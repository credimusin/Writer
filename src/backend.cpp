#include "backend.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMimeData>
#include <QPrinter>
#include <QProcess>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

#include "markdownhighlighter.h"

namespace {

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString defaultAuthorSetting = QStringLiteral("metadata/defaultAuthor");
const QString markdownFileFilter = QStringLiteral("Markdown files (*.md *.markdown);;All files (*)");

struct FrontmatterInfo {
    bool hasFrontmatter = false;
    int length = 0;
    QString title;
    QString author;
    QString topic;
    QString created;
    QString updated;
    QStringList otherLines;
};

QString timestamp(const QDateTime &time) {
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString fileCreationTimestamp(const QFileInfo &file) {
    const QDateTime birth = file.birthTime();
    return timestamp(birth.isValid() ? birth : file.lastModified());
}

QString unquoteYaml(const QString &value) {
    if (value.size() < 2 || value.front() != value.back())
        return value;

    const QString inner = value.mid(1, value.size() - 2);
    if (value.front() == QLatin1Char('\''))
        return QString(inner).replace(QStringLiteral("''"), QStringLiteral("'"));
    if (value.front() != QLatin1Char('"'))
        return value;

    QString result;
    for (int i = 0; i < inner.size(); ++i) {
        const bool escapedQuoteOrSlash = inner.at(i) == QLatin1Char('\\') && i + 1 < inner.size()
            && (inner.at(i + 1) == QLatin1Char('"') || inner.at(i + 1) == QLatin1Char('\\'));
        if (escapedQuoteOrSlash)
            ++i;
        result += inner.at(i);
    }
    return result;
}

QString yamlScalar(const QString &value) {
    static const QString indicators = QStringLiteral("-?:,[]{}#&*!|>'\"%@`");
    const bool plain = value == value.trimmed()
        && !indicators.contains(value.front())
        && !value.contains(QLatin1String(": "))
        && !value.contains(QLatin1String(" #"))
        && !value.endsWith(QLatin1Char(':'));
    if (plain)
        return value;

    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
           .replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

FrontmatterInfo parseFrontmatter(const QString &text) {
    FrontmatterInfo info;
    static const QRegularExpression frontmatterRe(
        QStringLiteral(R"(^---\r?\n([\s\S]*?)\r?\n(?:---|\.\.\.)(?:\r?\n|$))"));
    const QRegularExpressionMatch match = frontmatterRe.match(text);
    if (!match.hasMatch())
        return info;

    info.hasFrontmatter = true;
    info.length = match.capturedLength(0);

    static const QRegularExpression fieldRe(
        QStringLiteral(R"(^(title|author|topic|created|updated):[ \t]+([^\s|>\[{&*!#].*?)\s*$)"));
    const QStringList lines = match.captured(1).split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const QRegularExpressionMatch field = fieldRe.match(line);
        if (!field.hasMatch()) {
            info.otherLines.append(line);
            continue;
        }

        const QString key = field.captured(1);
        const QString value = unquoteYaml(field.captured(2));
        if (key == QLatin1String("title"))
            info.title = value;
        else if (key == QLatin1String("author"))
            info.author = value;
        else if (key == QLatin1String("topic"))
            info.topic = value;
        else if (key == QLatin1String("created"))
            info.created = value;
        else
            info.updated = value;
    }
    return info;
}

QString documentBody(const QString &fullText, const FrontmatterInfo &info) {
    if (!info.hasFrontmatter)
        return fullText;

    const QString body = fullText.mid(info.length);
    if (body.startsWith(QLatin1String("\r\n")))
        return body.mid(2);
    if (body.startsWith(QLatin1Char('\n')))
        return body.mid(1);
    return body;
}

QString firstLineTitle(const QString &text) {
    static const QRegularExpression headingMarkerRe(QStringLiteral("^#+\\s*"));
    QString line = text.trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
    return line.remove(headingMarkerRe).left(100);
}

QString sanitizeFileName(QString name) {
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

QRegularExpressionMatch matchHeading(const QString &text) {
    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
    return headingRe.match(text);
}

bool isVerbatimBlock(const QTextBlock &block) {
    const int state = block.userState();
    return state == MarkdownHighlighter::StateInCodeBlock
        || state == MarkdownHighlighter::StateInFrontmatter
        || MarkdownHighlighter::isFenceLine(block.text())
        || (block.blockNumber() == 0 && block.text().trimmed() == QLatin1String("---"));
}

void applyBlockTypography(QTextCursor &cursor, const QTextBlock &block) {
    const bool centered = !isVerbatimBlock(block)
        && (matchHeading(block.text()).hasMatch() || MarkdownHighlighter::isRuleLine(block.text()));

    QTextBlockFormat format;
    format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    format.setAlignment(centered ? Qt::AlignHCenter : Qt::AlignLeft);
    format.setTopMargin(0);
    format.setBottomMargin(0);

    cursor.setPosition(block.position());
    cursor.mergeBlockFormat(format);
}

void unwatchAll(QFileSystemWatcher &watcher) {
    const QStringList watched = watcher.files();
    if (!watched.isEmpty())
        watcher.removePaths(watched);
}

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
}

Backend::~Backend() = default;

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
    m_themeBackground = darkMode ? QStringLiteral("#fa101010") : QStringLiteral("#f5ffffff");
    m_themeForeground = darkMode ? QStringLiteral("#f5f1e8") : QStringLiteral("#101010");

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

    if (!m_fileUrl.isValid() && !m_metadata.hasMetadata)
        initNewDocumentMetadata();
}

void Backend::openDialog() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Open File"),
        QFileInfo(suggestedSavePath()).absolutePath(), markdownFileFilter);
    if (!path.isEmpty())
        open(QUrl::fromLocalFile(path));
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QFileInfo fileInfo(url.toLocalFile());
    QFile file(fileInfo.filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(fileInfo.fileName()));
        return;
    }

    const QByteArray contents = file.readAll();
    const QString fullText = QString::fromUtf8(contents);
    const FrontmatterInfo info = parseFrontmatter(fullText);

    m_metadata.hasMetadata = info.hasFrontmatter;
    m_metadata.title = info.title;
    m_metadata.author = info.author.isEmpty() ? defaultAuthor() : info.author;
    m_metadata.topic = info.topic;
    m_metadata.created = info.created.isEmpty() ? fileCreationTimestamp(fileInfo) : info.created;
    m_metadata.updated = info.updated.isEmpty() ? timestamp(fileInfo.lastModified()) : info.updated;
    m_metadata.otherLines = info.otherLines;
    loadDocumentText(documentBody(fullText, info));

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
        saveAsDialog();
        return;
    }

    saveAs(m_fileUrl);
}

void Backend::saveAsDialog() {
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Save File"), suggestedSavePath(), markdownFileFilter);
    if (path.isEmpty()) {
        emit saveFinished(false);
        return;
    }

    saveAs(QUrl::fromLocalFile(path));
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

void Backend::exportPdfDialog() {
    const QFileInfo source(suggestedSavePath());
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Export PDF"),
        source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral(".pdf")),
        QStringLiteral("PDF files (*.pdf);;All files (*)"));
    if (!path.isEmpty())
        exportPdf(path);
}

void Backend::exportPdf(const QString &path) {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to export."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);

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

    QTextCharFormat codeFormat;
    codeFormat.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});

    for (QTextBlock block = rendered->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();

        if (isVerbatimBlock(block)) {
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + text.length(), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(codeFormat);
            continue;
        }

        QList<QPair<int, int>> toDelete;

        const QRegularExpressionMatch heading = matchHeading(text);
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

        const QList<MarkdownHighlighter::InlineMarkup> markup = MarkdownHighlighter::inlineMarkup(text);
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
    QString text = currentDocumentText();
    bool isModified = true;

    if (m_hasKnownFileContents) {
        const QString fullText = QString::fromUtf8(m_lastKnownFileContents);
        const FrontmatterInfo info = parseFrontmatter(fullText);
        QString diskBody = documentBody(fullText, info);
        diskBody.remove(QLatin1Char('\r'));
        text.remove(QLatin1Char('\r'));

        const bool metaMatches = info.hasFrontmatter == m_metadata.hasMetadata
            && (!m_metadata.hasMetadata
                || (info.title == m_metadata.title
                    && (info.author.isEmpty() ? defaultAuthor() : info.author) == m_metadata.author
                    && info.topic == m_metadata.topic
                    && info.otherLines == m_metadata.otherLines));
        isModified = text != diskBody || !metaMatches;
    } else {
        const bool textEmpty = text.isEmpty() || text == QStringLiteral("# Start writing\n");
        isModified = !textEmpty || !m_metadata.title.isEmpty() || !m_metadata.topic.isEmpty();
    }

    setModified(isModified);
    if (isModified) {
        setStatus(QStringLiteral("Unsaved"));
        scheduleRecovery();
    } else {
        setStatus(m_fileUrl.isValid() ? QStringLiteral("Saved") : QString());
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

    if (m_document)
        reapplyTypographyToChange();

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
    if (!block.isValid() || isVerbatimBlock(block) || MarkdownHighlighter::isRuleLine(block.text()))
        return ranges;

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

void Backend::failSave(const QString &status) {
    setStatus(status);
    emit saveFinished(false);
}

void Backend::saveAs(const QUrl &url) {
    if (!url.isLocalFile()) {
        failSave(QStringLiteral("Only local files can be saved."));
        return;
    }

    QString localPath = url.toLocalFile();
    if (QFileInfo(localPath).suffix().isEmpty())
        localPath += QStringLiteral(".md");
    const QString targetName = QFileInfo(localPath).fileName();

    QString fileContent = currentDocumentText();
    if (m_metadata.hasMetadata) {
        if (m_metadata.title.trimmed().isEmpty()) {
            const QString headingTitle = firstLineTitle(fileContent);
            const QString fileBaseName = QFileInfo(localPath).completeBaseName();
            if (!headingTitle.isEmpty())
                m_metadata.title = headingTitle;
            else if (fileBaseName != QStringLiteral("Untitled"))
                m_metadata.title = fileBaseName;
        }
        if (m_metadata.author.isEmpty())
            m_metadata.author = defaultAuthor();
        const QString now = timestamp(QDateTime::currentDateTime());
        if (m_metadata.created.isEmpty())
            m_metadata.created = now;
        m_metadata.updated = now;

        fileContent = serializeFileContent(fileContent);
    }

    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        failSave(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = fileContent.toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    unwatchAll(m_fileWatcher);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        failSave(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    m_lastDocumentText = currentDocumentText();
    setFileUrl(QUrl::fromLocalFile(localPath));
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting, QFileInfo(localPath).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveFinished(true);
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

void Backend::writeRecovery() {
    if (!m_modified || m_recoveryPath.isEmpty())
        return;

    QSaveFile file(m_recoveryPath);
    if (!file.open(QIODevice::WriteOnly))
        return;

    const QJsonObject metadata{
        {QStringLiteral("hasMetadata"), m_metadata.hasMetadata},
        {QStringLiteral("title"), m_metadata.title},
        {QStringLiteral("author"), m_metadata.author},
        {QStringLiteral("topic"), m_metadata.topic},
        {QStringLiteral("created"), m_metadata.created},
        {QStringLiteral("updated"), m_metadata.updated},
        {QStringLiteral("otherLines"), QJsonArray::fromStringList(m_metadata.otherLines)},
    };
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()},
                               {QStringLiteral("metadata"), metadata}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(m_recoveryPath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());

    if (recovery.contains(QStringLiteral("metadata"))) {
        const QJsonObject metadata = recovery.value(QStringLiteral("metadata")).toObject();
        m_metadata.hasMetadata = metadata.value(QStringLiteral("hasMetadata")).toBool();
        m_metadata.title = metadata.value(QStringLiteral("title")).toString();
        m_metadata.author = metadata.value(QStringLiteral("author")).toString();
        m_metadata.topic = metadata.value(QStringLiteral("topic")).toString();
        m_metadata.created = metadata.value(QStringLiteral("created")).toString();
        m_metadata.updated = metadata.value(QStringLiteral("updated")).toString();
        m_metadata.otherLines.clear();
        const QJsonArray otherLines = metadata.value(QStringLiteral("otherLines")).toArray();
        for (const QJsonValue &line : otherLines)
            m_metadata.otherLines.append(line.toString());
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
    if (!m_recoveryPath.isEmpty())
        QFile::remove(m_recoveryPath);
}

void Backend::watchCurrentFile() {
    unwatchAll(m_fileWatcher);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

QString Backend::suggestedSavePath() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl.toLocalFile();

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    const QString title = m_metadata.title.trimmed();
    return directory.filePath(title.isEmpty() ? suggestedFileName(currentDocumentText())
                                              : sanitizeFileName(title));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    const QString body = documentBody(text, parseFrontmatter(text));
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
    const QString now = timestamp(QDateTime::currentDateTime());
    m_metadata = Metadata{};
    m_metadata.hasMetadata = true;
    m_metadata.author = defaultAuthor();
    m_metadata.created = now;
    m_metadata.updated = now;
}

QString Backend::serializeFileContent(const QString &bodyText) const {
    QString yaml = QStringLiteral("---\n");
    const auto writeField = [this, &yaml](const QString &key, const QString &value) {
        const QString keyPrefix = key + QLatin1Char(':');
        const bool keptVerbatim = std::any_of(
            m_metadata.otherLines.cbegin(), m_metadata.otherLines.cend(),
            [&keyPrefix](const QString &line) { return line.startsWith(keyPrefix); });
        if (!value.isEmpty() && !keptVerbatim)
            yaml += key + QStringLiteral(": ") + yamlScalar(value) + QLatin1Char('\n');
    };
    writeField(QStringLiteral("title"), m_metadata.title);
    writeField(QStringLiteral("author"), m_metadata.author);
    writeField(QStringLiteral("topic"), m_metadata.topic);
    writeField(QStringLiteral("created"), m_metadata.created);
    writeField(QStringLiteral("updated"), m_metadata.updated);
    for (const QString &line : m_metadata.otherLines)
        yaml += line + QLatin1Char('\n');

    return yaml + QStringLiteral("---\n\n") + bodyText;
}

QString Backend::suggestedFileName(const QString &text) {
    const FrontmatterInfo info = parseFrontmatter(text);
    const QString title = info.title.trimmed();
    return sanitizeFileName(title.isEmpty() ? firstLineTitle(documentBody(text, info)) : title);
}

QString Backend::defaultAuthor() const {
    const QString saved = QSettings().value(defaultAuthorSetting).toString();
    if (!saved.isEmpty())
        return saved;
    const QString envUser = qEnvironmentVariable("USER");
    if (!envUser.isEmpty())
        return envUser;
    return qEnvironmentVariable("USERNAME");
}

void Backend::setDefaultAuthor(const QString &author) {
    QSettings().setValue(defaultAuthorSetting, author);
}

QVariantMap Backend::documentMetadata() const {
    QString title = m_metadata.title;
    if (title.isEmpty()) {
        title = m_fileUrl.isLocalFile()
            ? QFileInfo(m_fileUrl.toLocalFile()).completeBaseName()
            : firstLineTitle(currentDocumentText());
    }

    return {
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), m_metadata.author.isEmpty() ? defaultAuthor() : m_metadata.author},
        {QStringLiteral("topic"), m_metadata.topic},
        {QStringLiteral("created"), m_metadata.created},
        {QStringLiteral("updated"), m_metadata.updated},
    };
}

void Backend::updateDocumentMetadata(const QString &title, const QString &author, const QString &topic) {
    if (!author.isEmpty())
        setDefaultAuthor(author);

    m_metadata.hasMetadata = true;
    m_metadata.title = title;
    m_metadata.author = author.isEmpty() ? defaultAuthor() : author;
    m_metadata.topic = topic;
    const QString now = timestamp(QDateTime::currentDateTime());
    if (m_metadata.created.isEmpty())
        m_metadata.created = now;
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
    return m_document && isVerbatimBlock(m_document->findBlock(cursorPosition));
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);
    m_formattingTypography = true;

    QTextCursor cursor(m_document);
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next())
        applyBlockTypography(cursor, block);

    m_formattingTypography = false;
    m_document->setUndoRedoEnabled(undoEnabled);
}

void Backend::reapplyTypographyToChange() {
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();

    const QTextBlock endBlock = m_document->findBlock(end);
    for (QTextBlock block = m_document->findBlock(start); block.isValid(); block = block.next()) {
        applyBlockTypography(cursor, block);
        if (block == endBlock)
            break;
    }

    cursor.endEditBlock();
    m_formattingTypography = false;
}
