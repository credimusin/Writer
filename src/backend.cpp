#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");

struct FrontmatterInfo {
    bool hasFrontmatter = false;
    int length = 0;
    QString title;
    QString author;
    QString topic;
    QString created;
    QString updated;
    QList<QPair<QString, QString>> otherFields;
};

static FrontmatterInfo parseFrontmatter(const QString &text) {
    FrontmatterInfo info;
    static const QRegularExpression frontmatterRe(
        QStringLiteral(R"(^---\r?\n([\s\S]*?)\r?\n(?:---|\.\.\.)(?:\r?\n|$))")
    );
    const QRegularExpressionMatch match = frontmatterRe.match(text);
    if (!match.hasMatch()) {
        return info;
    }

    info.hasFrontmatter = true;
    info.length = match.capturedLength(0);

    const QString content = match.captured(1);
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;

        const QString key = line.left(colon).trimmed().toLower();
        QString value = line.mid(colon + 1).trimmed();
        if (value.size() >= 2 && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                               || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2).trimmed();
        }

        if (key == QStringLiteral("title")) {
            info.title = value;
        } else if (key == QStringLiteral("author")) {
            info.author = value;
        } else if (key == QStringLiteral("topic") || key == QStringLiteral("theme") || key == QStringLiteral("subject") || key == QStringLiteral("tags") || key == QStringLiteral("tag")) {
            info.topic = value;
        } else if (key == QStringLiteral("created") || key == QStringLiteral("date") || key == QStringLiteral("creation_date")) {
            info.created = value;
        } else if (key == QStringLiteral("updated") || key == QStringLiteral("lastmod") || key == QStringLiteral("modified") || key == QStringLiteral("update_date")) {
            info.updated = value;
        } else {
            info.otherFields.append({rawLine.left(rawLine.indexOf(QLatin1Char(':'))).trimmed(), value});
        }
    }
    return info;
}

static QString sanitizeFileName(QString name) {
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    QUrl url = QUrl::fromUserInput(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    
    if (m_darkMode) {
        m_themeBackground = QStringLiteral("#fa101010"); // ~98% opacity
        m_themeForeground = QStringLiteral("#f5f1e8");
        m_themeSelection = m_themeAccent;
    } else {
        m_themeBackground = QStringLiteral("#f5ffffff"); // ~96% opacity
        m_themeForeground = QStringLiteral("#101010");
        m_themeSelection = m_themeAccent;
    }
    
    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }
    
    emit themeColorsChanged();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    restoreRecovery();

    if (!m_fileUrl.isValid() && !m_metadata.hasMetadata) {
        initNewDocumentMetadata();
    }
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    const QString fullText = QString::fromUtf8(contents);
    const FrontmatterInfo info = parseFrontmatter(fullText);

    if (info.hasFrontmatter) {
        m_metadata.hasMetadata = true;
        m_metadata.title = info.title;
        m_metadata.author = info.author.isEmpty() ? defaultAuthor() : info.author;
        m_metadata.topic = info.topic;
        m_metadata.created = info.created;
        m_metadata.updated = info.updated;
        m_metadata.otherFields = info.otherFields;

        QString body = fullText.mid(info.length);
        if (body.startsWith(QLatin1String("\r\n")))
            body = body.mid(2);
        else if (body.startsWith(QLatin1Char('\n')))
            body = body.mid(1);
        loadDocumentText(body);
    } else {
        m_metadata.hasMetadata = false;
        m_metadata.title.clear();
        m_metadata.author = defaultAuthor();
        m_metadata.topic.clear();
        const QFileInfo fi(url.toLocalFile());
        QDateTime bTime = fi.birthTime();
        if (!bTime.isValid()) bTime = fi.lastModified();
        m_metadata.created = bTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        m_metadata.updated = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        m_metadata.otherFields.clear();

        loadDocumentText(fullText);
    }

    clearRecovery();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        emit saveAsRequested();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::exportPdf(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be saved as PDF."));
        return;
    }
    
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to export."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(url.toLocalFile());

    // Clone to preserve the editor's exact layout (alignment, line heights, explicit empty lines)
    std::unique_ptr<QTextDocument> rendered(m_document->clone());
    MarkdownHighlighter highlighter(rendered.get()); // Highlights the clone and sets block userStates

    QFont font = rendered->defaultFont();
    if (font.pixelSize() > 0) {
        font.setPointSize(qMax(10, font.pixelSize() * 72 / 96));
        font.setPixelSize(-1);
    }
    rendered->setDefaultFont(font);

    QTextCursor cursor(rendered.get());
    cursor.select(QTextCursor::Document);
    QTextCharFormat charFormat;
    charFormat.setForeground(Qt::black);
    cursor.mergeCharFormat(charFormat);

    for (QTextBlock block = rendered->begin(); block.isValid(); block = block.next()) {
        QString text = block.text();
        
        const bool inCodeBlock = (block.userState() == MarkdownHighlighter::StateInCodeBlock)
            || MarkdownHighlighter::isFenceLine(text)
            || MarkdownHighlighter::isClosingFence(text);

        if (inCodeBlock) {
            QTextCharFormat codeFmt;
            codeFmt.setFontFamilies({QStringLiteral("monospace")});
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + text.length(), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(codeFmt);
            continue;
        }

        QList<QPair<int, int>> toDelete;

        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            int level = heading.capturedLength(1);
            qreal scale = 1.0 + (6.0 - level) * 0.2;
            const qreal baseSize = font.pointSizeF() > 0 ? font.pointSizeF()
                : (font.pixelSize() > 0 ? font.pixelSize() * 72.0 / 96.0 : 12.0);
            QTextCharFormat fmt;
            fmt.setFontPointSize(baseSize * scale);
            fmt.setFontWeight(QFont::Bold);

            cursor.setPosition(block.position() + heading.capturedStart(3));
            cursor.setPosition(block.position() + heading.capturedStart(3) + heading.capturedLength(3), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(fmt);

            toDelete.append({heading.capturedStart(1), heading.capturedLength(1) + heading.capturedLength(2)});
        }

        QList<MarkdownHighlighter::InlineMarkup> markup = MarkdownHighlighter::inlineMarkup(text);
        for (const auto &item : markup) {
            QTextCharFormat contentFmt;
            if (item.kind == MarkdownHighlighter::InlineKind::Bold || item.kind == MarkdownHighlighter::InlineKind::BoldItalic)
                contentFmt.setFontWeight(QFont::Bold);
            if (item.kind == MarkdownHighlighter::InlineKind::Italic || item.kind == MarkdownHighlighter::InlineKind::BoldItalic)
                contentFmt.setFontItalic(true);

            cursor.setPosition(block.position() + item.content.start);
            cursor.setPosition(block.position() + item.content.start + item.content.length, QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(contentFmt);

            for (const auto &marker : item.markers) {
                if (marker.length > 0)
                    toDelete.append({marker.start, marker.length});
            }
        }

        std::sort(toDelete.begin(), toDelete.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
            return a.first > b.first;
        });

        for (const auto &del : std::as_const(toDelete)) {
            cursor.setPosition(block.position() + del.first);
            cursor.setPosition(block.position() + del.first + del.second, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
    }

    rendered->print(&printer);
    
    setStatus(QStringLiteral("Exported to PDF"));
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

void Backend::updateModifiedState() {
    const QString text = currentDocumentText();
    bool isModified = true;

    if (m_hasKnownFileContents) {
        const QString fullText = QString::fromUtf8(m_lastKnownFileContents);
        const FrontmatterInfo info = parseFrontmatter(fullText);
        QString diskBody = info.hasFrontmatter ? fullText.mid(info.length) : fullText;
        if (diskBody.startsWith(QLatin1String("\r\n")))
            diskBody = diskBody.mid(2);
        else if (diskBody.startsWith(QLatin1Char('\n')))
            diskBody = diskBody.mid(1);
        diskBody.remove(QLatin1Char('\r'));

        QString currentNormalized = text;
        currentNormalized.remove(QLatin1Char('\r'));

        const bool textMatches = (currentNormalized == diskBody);
        bool metaMatches = true;
        if (info.hasFrontmatter != m_metadata.hasMetadata
                || info.title != m_metadata.title
                || (info.author.isEmpty() ? defaultAuthor() : info.author) != m_metadata.author
                || info.topic != m_metadata.topic
                || info.otherFields != m_metadata.otherFields) {
            metaMatches = false;
        }

        if (textMatches && metaMatches) {
            isModified = false;
        }
    } else {
        const bool textEmpty = text.isEmpty() || text == QStringLiteral("# Start writing\n");
        const bool metaEmpty = m_metadata.title.isEmpty() && m_metadata.topic.isEmpty();
        if (textEmpty && metaEmpty) {
            isModified = false;
        }
    }

    setModified(isModified);
    if (isModified) {
        setStatus(QStringLiteral("Unsaved"));
        scheduleRecovery();
    } else {
        setStatus(m_fileUrl.isValid() ? QStringLiteral("Saved") : QStringLiteral(""));
        clearRecovery();
    }
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        reapplyTypographyToChange();
        m_formattedBlockCount = m_document->blockCount();
    }

    scheduleWordCount();
    updateModifiedState();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    if (block.userState() == MarkdownHighlighter::StateInCodeBlock
            || block.userState() == MarkdownHighlighter::StateInFrontmatter
            || MarkdownHighlighter::isFenceLine(block.text())
            || MarkdownHighlighter::isClosingFence(block.text())
            || (block.blockNumber() == 0 && block.text().trimmed() == QStringLiteral("---"))
            || MarkdownHighlighter::isRuleLine(block.text())) {
        return ranges;
    }

    const int lineStart = block.position();
    struct MarkerSpan {
        int start;
        int end;
        int markupId;
        bool operator<(const MarkerSpan &other) const {
            return start < other.start;
        }
    };
    QList<MarkerSpan> spans;
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    int currentId = 0;
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        if (item.content.length == 0) continue;
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length, currentId});
        }
        currentId++;
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.start},
                                  {QStringLiteral("end"), span.end},
                                  {QStringLiteral("id"), span.markupId}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}


void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    QString localPath = url.toLocalFile();
    if (QFileInfo(localPath).suffix().isEmpty()) {
        localPath += QStringLiteral(".md");
    }
    const QString targetName = QFileInfo(localPath).fileName();
    const QString fileBaseName = QFileInfo(localPath).completeBaseName();
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    if (m_metadata.title.trimmed().isEmpty()) {
        QString body = currentDocumentText();
        QString firstLine = body.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (firstLine.startsWith(QLatin1Char('#'))) {
            firstLine = firstLine.remove(QRegularExpression(QStringLiteral("^#+\\s*"))).trimmed();
        }
        if (!firstLine.isEmpty()) {
            m_metadata.title = firstLine.left(100);
        } else if (fileBaseName != QStringLiteral("Untitled")) {
            m_metadata.title = fileBaseName;
        }
    }
    if (m_metadata.author.isEmpty()) {
        m_metadata.author = defaultAuthor();
    }
    if (m_metadata.created.isEmpty()) {
        m_metadata.created = now;
    }
    m_metadata.updated = now;
    m_metadata.hasMetadata = true;

    const QString fileContent = serializeFileContent(currentDocumentText());

    QUrl finalUrl = QUrl::fromLocalFile(localPath);
    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = fileContent.toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    m_lastDocumentText = currentDocumentText();
    setFileUrl(finalUrl);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(localPath).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonObject metaObj;
    metaObj[QStringLiteral("hasMetadata")] = m_metadata.hasMetadata;
    metaObj[QStringLiteral("title")] = m_metadata.title;
    metaObj[QStringLiteral("author")] = m_metadata.author;
    metaObj[QStringLiteral("topic")] = m_metadata.topic;
    metaObj[QStringLiteral("created")] = m_metadata.created;
    metaObj[QStringLiteral("updated")] = m_metadata.updated;

    if (!m_metadata.otherFields.isEmpty()) {
        QJsonArray otherArr;
        for (const auto &pair : m_metadata.otherFields) {
            QJsonObject fieldObj;
            fieldObj[QStringLiteral("key")] = pair.first;
            fieldObj[QStringLiteral("value")] = pair.second;
            otherArr.append(fieldObj);
        }
        metaObj[QStringLiteral("otherFields")] = otherArr;
    }

    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()},
                               {QStringLiteral("metadata"), metaObj}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());

    if (recovery.contains(QStringLiteral("metadata"))) {
        QJsonObject metaObj = recovery.value(QStringLiteral("metadata")).toObject();
        m_metadata.hasMetadata = metaObj.value(QStringLiteral("hasMetadata")).toBool();
        m_metadata.title = metaObj.value(QStringLiteral("title")).toString();
        m_metadata.author = metaObj.value(QStringLiteral("author")).toString();
        m_metadata.topic = metaObj.value(QStringLiteral("topic")).toString();
        m_metadata.created = metaObj.value(QStringLiteral("created")).toString();
        m_metadata.updated = metaObj.value(QStringLiteral("updated")).toString();
        m_metadata.otherFields.clear();
        if (metaObj.contains(QStringLiteral("otherFields"))) {
            const QJsonArray otherArr = metaObj.value(QStringLiteral("otherFields")).toArray();
            for (const auto &val : otherArr) {
                const QJsonObject fieldObj = val.toObject();
                m_metadata.otherFields.append({fieldObj.value(QStringLiteral("key")).toString(),
                                               fieldObj.value(QStringLiteral("value")).toString()});
            }
        }
    }

    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}




QUrl Backend::suggestedFolder() const {
    if (m_fileUrl.isLocalFile())
        return QUrl::fromLocalFile(QFileInfo(m_fileUrl.toLocalFile()).absolutePath());

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(directory.absolutePath());
}

QUrl Backend::suggestedPdfUrl() const {
    if (m_fileUrl.isLocalFile()) {
        const QFileInfo fi(m_fileUrl.toLocalFile());
        return QUrl::fromLocalFile(fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".pdf")));
    }
    const QUrl saveUrl = suggestedSaveUrl();
    const QFileInfo fi(saveUrl.toLocalFile());
    return QUrl::fromLocalFile(fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".pdf")));
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);

    QString name;
    if (!m_metadata.title.trimmed().isEmpty()) {
        name = sanitizeFileName(m_metadata.title.trimmed());
    } else {
        name = suggestedFileName(currentDocumentText());
    }

    return QUrl::fromLocalFile(directory.filePath(name));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    const FrontmatterInfo info = parseFrontmatter(text);
    QString body = text;
    if (info.hasFrontmatter && info.length <= text.length()) {
        body = text.mid(info.length);
    }
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(body);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

void Backend::initNewDocumentMetadata() {
    m_metadata.hasMetadata = true;
    m_metadata.author = defaultAuthor();
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_metadata.created = now;
    m_metadata.updated = now;
    m_metadata.title.clear();
    m_metadata.topic.clear();
    m_metadata.otherFields.clear();
}

QString Backend::serializeFileContent(const QString &bodyText) const {
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    const QString titleToSet = m_metadata.title;
    const QString authorStr = m_metadata.author.isEmpty() ? defaultAuthor() : m_metadata.author;
    const QString createdStr = m_metadata.created.isEmpty() ? now : m_metadata.created;
    const QString updatedStr = now;

    QString yaml = QStringLiteral("---\n");
    if (!titleToSet.isEmpty())
        yaml += QStringLiteral("title: %1\n").arg(titleToSet);
    if (!authorStr.isEmpty())
        yaml += QStringLiteral("author: %1\n").arg(authorStr);
    if (!m_metadata.topic.isEmpty())
        yaml += QStringLiteral("topic: %1\n").arg(m_metadata.topic);
    yaml += QStringLiteral("created: %1\n").arg(createdStr);
    yaml += QStringLiteral("updated: %1\n").arg(updatedStr);

    for (const auto &pair : m_metadata.otherFields) {
        yaml += QStringLiteral("%1: %2\n").arg(pair.first, pair.second);
    }
    yaml += QStringLiteral("---\n\n");

    return yaml + bodyText;
}

QString Backend::suggestedFileName(const QString &text) {
    const FrontmatterInfo info = parseFrontmatter(text);
    QString name;
    if (info.hasFrontmatter && !info.title.trimmed().isEmpty()) {
        name = info.title.trimmed();
    } else {
        QString body = text;
        if (info.hasFrontmatter && info.length <= body.length()) {
            body = body.mid(info.length).trimmed();
        }
        name = body.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (name.startsWith(QLatin1Char('#'))) {
            name = name.remove(QRegularExpression(QStringLiteral("^#+\\s*"))).trimmed();
        }
    }
    return sanitizeFileName(name);
}

QString Backend::defaultAuthor() const {
    const QString saved = QSettings().value(QStringLiteral("metadata/defaultAuthor")).toString();
    if (!saved.isEmpty())
        return saved;
    const QString envUser = qEnvironmentVariable("USER");
    if (!envUser.isEmpty())
        return envUser;
    return qEnvironmentVariable("USERNAME");
}

void Backend::setDefaultAuthor(const QString &author) {
    QSettings().setValue(QStringLiteral("metadata/defaultAuthor"), author);
}

QVariantMap Backend::documentMetadata() const {
    QVariantMap meta;
    meta[QStringLiteral("hasFrontmatter")] = m_metadata.hasMetadata;

    QString defaultTitle = m_metadata.title;
    if (defaultTitle.isEmpty()) {
        if (m_fileUrl.isLocalFile()) {
            const QFileInfo fi(m_fileUrl.toLocalFile());
            defaultTitle = fi.completeBaseName();
        } else {
            const QString text = currentDocumentText();
            QString firstLine = text.section(QLatin1Char('\n'), 0, 0).trimmed();
            if (firstLine.startsWith(QLatin1Char('#'))) {
                defaultTitle = firstLine.remove(QRegularExpression(QStringLiteral("^#+\\s*"))).trimmed();
            } else if (!firstLine.isEmpty()) {
                defaultTitle = firstLine.left(60);
            }
        }
    }
    meta[QStringLiteral("title")] = defaultTitle;
    meta[QStringLiteral("author")] = m_metadata.author.isEmpty() ? defaultAuthor() : m_metadata.author;
    meta[QStringLiteral("topic")] = m_metadata.topic;

    QString createdTime = m_metadata.created;
    QString updatedTime = m_metadata.updated;
    if (createdTime.isEmpty()) {
        if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile())) {
            const QFileInfo fi(m_fileUrl.toLocalFile());
            QDateTime bTime = fi.birthTime();
            if (!bTime.isValid()) bTime = fi.lastModified();
            createdTime = bTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            updatedTime = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        } else {
            const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            createdTime = now;
            updatedTime = now;
        }
    }
    meta[QStringLiteral("created")] = createdTime;
    meta[QStringLiteral("updated")] = updatedTime;

    return meta;
}

void Backend::updateDocumentMetadata(const QString &title, const QString &author, const QString &topic) {
    if (!author.isEmpty()) {
        setDefaultAuthor(author);
    }
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    m_metadata.hasMetadata = true;
    m_metadata.title = title;
    m_metadata.author = author.isEmpty() ? defaultAuthor() : author;
    m_metadata.topic = topic;
    if (m_metadata.created.isEmpty()) {
        m_metadata.created = now;
    }
    m_metadata.updated = now;

    updateModifiedState();
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

bool Backend::isInCodeBlock(int cursorPosition) const {
    if (!m_document) return false;
    QTextBlock block = m_document->findBlock(cursorPosition);
    if (!block.isValid()) return false;
    return (block.userState() == MarkdownHighlighter::StateInCodeBlock)
        || (block.userState() == MarkdownHighlighter::StateInFrontmatter)
        || MarkdownHighlighter::isFenceLine(block.text())
        || MarkdownHighlighter::isClosingFence(block.text())
        || (block.blockNumber() == 0 && block.text().trimmed() == QStringLiteral("---"));
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        const bool inSpecial = (block.userState() == MarkdownHighlighter::StateInCodeBlock)
            || (block.userState() == MarkdownHighlighter::StateInFrontmatter)
            || MarkdownHighlighter::isFenceLine(text)
            || MarkdownHighlighter::isClosingFence(text)
            || (block.blockNumber() == 0 && text.trimmed() == QStringLiteral("---"));

        QTextBlockFormat bf = blockFormat;
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}\\*(?:\\s*\\*){2,}\\s*$"));
        if (!inSpecial && (headingRe.match(text).hasMatch() || ruleRe.match(text).hasMatch())) {
            bf.setAlignment(Qt::AlignHCenter);
            bf.clearBackground();
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
        } else {
            bf.setAlignment(Qt::AlignLeft);
            bf.clearBackground();
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
        }
        cursor.setPosition(block.position());
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.mergeBlockFormat(bf);
    }
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    
    QTextBlock block = m_document->findBlock(start);
    QTextBlock endBlock = m_document->findBlock(end);
    
    while (block.isValid() && block.position() <= endBlock.position()) {
        const QString text = block.text();
        const bool inSpecial = (block.userState() == MarkdownHighlighter::StateInCodeBlock)
            || (block.userState() == MarkdownHighlighter::StateInFrontmatter)
            || MarkdownHighlighter::isFenceLine(text)
            || MarkdownHighlighter::isClosingFence(text)
            || (block.blockNumber() == 0 && text.trimmed() == QStringLiteral("---"));

        QTextBlockFormat bf = blockFormat;
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}\\*(?:\\s*\\*){2,}\\s*$"));
        if (!inSpecial && (headingRe.match(text).hasMatch() || ruleRe.match(text).hasMatch())) {
            bf.setAlignment(Qt::AlignHCenter);
            bf.clearBackground();
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
        } else {
            bf.setAlignment(Qt::AlignLeft);
            bf.clearBackground();
            bf.setTopMargin(0);
            bf.setBottomMargin(0);
        }
        cursor.setPosition(block.position());
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.mergeBlockFormat(bf);
        
        if (block == endBlock) break;
        block = block.next();
    }
    
    cursor.endEditBlock();
    m_formattingTypography = false;
}
