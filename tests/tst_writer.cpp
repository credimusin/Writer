#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "lspclient.h"
#include "markdownhighlighter.h"
#include "systemtheme.h"

static void writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

static QString readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

class WriterTest : public QObject {
    Q_OBJECT

private:
    QObject *loadEditor(QQmlEngine &engine, Backend &backend, QScopedPointer<QObject> &window) {
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("systemTheme"), &m_systemTheme);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        window.reset(component.create());
        if (!window) {
            qWarning().noquote() << component.errorString();
            return nullptr;
        }
        return window->findChild<QObject *>(QStringLiteral("sourceEditor"));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("writer"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("writer.local"));
        QCoreApplication::setApplicationName(QStringLiteral("writer"));
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("http://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("not a valid url")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("# Document Heading\nBody text")),
                 QStringLiteral("Document Heading.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("---\ntitle: Article Title\nauthor: Me\n---\n# Some Heading")),
                 QStringLiteral("Article Title.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void handlesHorizontalRulesAsterisksOnly() {
        QVERIFY(MarkdownHighlighter::isRuleLine(QStringLiteral("***")));
        QVERIFY(MarkdownHighlighter::isRuleLine(QStringLiteral("****")));
        QVERIFY(MarkdownHighlighter::isRuleLine(QStringLiteral("* * *")));
        QVERIFY(MarkdownHighlighter::isRuleLine(QStringLiteral("   ***   ")));

        QVERIFY(!MarkdownHighlighter::isRuleLine(QStringLiteral("---")));
        QVERIFY(!MarkdownHighlighter::isRuleLine(QStringLiteral("___")));
        QVERIFY(!MarkdownHighlighter::isRuleLine(QStringLiteral("**bold**")));
        QVERIFY(!MarkdownHighlighter::isRuleLine(QStringLiteral("***bold italic***")));
        QVERIFY(!MarkdownHighlighter::isRuleLine(QStringLiteral("* list item")));
    }

    void handlesFrontmatterInHighlighter() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral(
            "---\n"
            "title: My Post\n"
            "author: Alice\n"
            "topic: Testing\n"
            "---\n"
            "# Actual Heading\n"));
        MarkdownHighlighter highlighter(&doc);

        for (int block = 0; block <= 3; ++block)
            QCOMPARE(doc.findBlockByNumber(block).userState(), int(MarkdownHighlighter::StateInFrontmatter));
        QCOMPARE(doc.findBlockByNumber(4).userState(), int(MarkdownHighlighter::StateNormal));
        QCOMPARE(doc.findBlockByNumber(5).userState(), int(MarkdownHighlighter::StateNormal));
    }

    void ignoresFormattingInsideInlineCode() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("Here is `int *a = b * c; [not link](url) **not bold**` and *italic*"));
        QCOMPARE(markup.size(), 1);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(markup.at(0).content.start, 60);
        QCOMPARE(markup.at(0).content.length, 6);
    }

    void allowsCodeInsideInlineFormatting() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold with `code` inside** and [`link with code`](https://example.com)"));
        QCOMPARE(markup.size(), 2);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 23);
        QCOMPARE(markup.at(1).kind, MarkdownHighlighter::InlineKind::Link);
        QCOMPARE(markup.at(1).content.start, 33);
        QCOMPARE(markup.at(1).content.length, 16);
    }

    void handlesFencedCodeBlocksInHighlighter() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral(
            "# Heading 1\n"
            "```python\n"
            "# not a heading\n"
            "x = 1 * 2 * 3\n"
            "```\n"
            "# Heading 2\n"));
        MarkdownHighlighter highlighter(&doc);

        QCOMPARE(doc.findBlockByNumber(0).userState(), int(MarkdownHighlighter::StateNormal));
        for (int block = 1; block <= 3; ++block)
            QCOMPARE(doc.findBlockByNumber(block).userState(), int(MarkdownHighlighter::StateInCodeBlock));
        QCOMPARE(doc.findBlockByNumber(4).userState(), int(MarkdownHighlighter::StateNormal));
        QCOMPARE(doc.findBlockByNumber(5).userState(), int(MarkdownHighlighter::StateNormal));
    }

    void savesEditedMetadataAsFrontmatter() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("# My Story\nHello world"));
        QCOMPARE(backend.documentMetadata().value("title").toString(), QStringLiteral("My Story"));

        backend.updateDocumentMetadata(QStringLiteral("Part 1: New Title"), QStringLiteral("Author X"),
                                       QStringLiteral("Fiction"));
        QCOMPARE(backend.defaultAuthor(), QStringLiteral("Author X"));

        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Story.md"));
        backend.saveAs(QUrl::fromLocalFile(filePath));

        const QString saved = readFile(filePath);
        QVERIFY2(saved.startsWith(QStringLiteral(
                     "---\ntitle: \"Part 1: New Title\"\nauthor: Author X\ntopic: Fiction\n")),
                 qPrintable(saved));
        QVERIFY(saved.endsWith(QStringLiteral("---\n\n# My Story\nHello world")));

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(backend.documentMetadata().value("title").toString(), QStringLiteral("Part 1: New Title"));
    }

    void autoFillsMetadataForNewDocument() {
        Backend backend;
        backend.setDefaultAuthor(QStringLiteral("Auto Author"));
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);
        QCOMPARE(backend.modified(), false);

        const QString body = QStringLiteral("# Automatic Novel\nSome actual body content here.");
        editor->setProperty("text", body);
        QCOMPARE(backend.modified(), true);

        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Automatic Novel.md"));
        backend.saveAs(QUrl::fromLocalFile(filePath));

        const QString saved = readFile(filePath);
        QVERIFY(saved.contains(QStringLiteral("title: Automatic Novel\n")));
        QVERIFY(saved.contains(QStringLiteral("author: Auto Author\n")));
        QVERIFY(saved.contains(QStringLiteral("created: ")));

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(editor->property("text").toString(), body);
    }

    void preservesUnknownFrontmatterVerbatim() {
        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Custom.md"));
        const QByteArray unknownFields =
            "date: 2024-01-01\n"
            "tags:\n"
            "  - space\n"
            "  - opera\n"
            "params:\n"
            "  author: Ghost\n"
            "draft: true\n";
        writeFile(filePath, "---\ntitle: Custom Fields\nauthor: Alice\n" + unknownFields
                                + "---\n\n# Chapter 1\n");

        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Chapter 1\n"));
        QCOMPARE(backend.documentMetadata().value("author").toString(), QStringLiteral("Alice"));

        backend.save();
        const QString saved = readFile(filePath);
        QVERIFY2(saved.contains(QString::fromUtf8(unknownFields)), qPrintable(saved));
    }

    void handlesCrlfLineEndings() {
        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("CRLF.md"));
        writeFile(filePath,
                  "---\r\n"
                  "title: Windows Doc\r\n"
                  "author: Bob\r\n"
                  "---\r\n\r\n"
                  "# Windows Chapter\r\n"
                  "Text with CRLF\r\n");

        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Windows Chapter\nText with CRLF\n"));
        QCOMPARE(backend.modified(), false);
    }

    void tracksMetadataModifications() {
        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Track.md"));
        writeFile(filePath,
                  "---\n"
                  "title: Original Title\n"
                  "author: Alice\n"
                  "---\n\n"
                  "# Original Heading\n"
                  "Body content.\n");

        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(backend.modified(), false);

        backend.updateDocumentMetadata(QStringLiteral("Altered Title"), QStringLiteral("Alice"), QString());
        QCOMPARE(backend.modified(), true);

        editor->setProperty("text", QStringLiteral("# Original Heading\nBody content.\nExtra"));
        QCOMPARE(backend.modified(), true);
        editor->setProperty("text", QStringLiteral("# Original Heading\nBody content.\n"));
        QCOMPARE(backend.modified(), true);

        backend.updateDocumentMetadata(QStringLiteral("Original Title"), QStringLiteral("Alice"), QString());
        QCOMPARE(backend.modified(), false);
    }

    void keepsPlainFileWithoutFrontmatter() {
        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Plain.md"));
        writeFile(filePath, "# Plain Document\nNo frontmatter here.\n");

        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(backend.modified(), false);

        editor->setProperty("text", QStringLiteral("# Plain Document\nUpdated body.\n"));
        QCOMPARE(backend.modified(), true);
        backend.save();

        QCOMPARE(backend.modified(), false);
        QCOMPARE(readFile(filePath), QStringLiteral("# Plain Document\nUpdated body.\n"));
    }

    void addsFrontmatterWhenPropertiesAppliedToPlainFile() {
        QTemporaryDir tempDir;
        const QString filePath = tempDir.filePath(QStringLiteral("Plain.md"));
        writeFile(filePath, "# Plain Document\n");

        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QVERIFY(loadEditor(engine, backend, window));

        backend.open(QUrl::fromLocalFile(filePath));
        backend.updateDocumentMetadata(QStringLiteral("Explicit Title"), QStringLiteral("Author Y"), QString());
        QCOMPARE(backend.modified(), true);
        backend.save();

        const QString saved = readFile(filePath);
        QVERIFY(saved.startsWith(QStringLiteral("---\ntitle: Explicit Title\n")));
        QVERIFY(saved.endsWith(QStringLiteral("---\n\n# Plain Document\n")));
    }

    void centersOnlyHeadingBlocks() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("Plain line\n# Heading\nNext line"));

        auto *quickDocument = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(quickDocument);
        const QTextDocument *document = quickDocument->textDocument();
        QCOMPARE(document->findBlockByNumber(0).blockFormat().alignment(), Qt::Alignment(Qt::AlignLeft));
        QCOMPARE(document->findBlockByNumber(1).blockFormat().alignment(), Qt::Alignment(Qt::AlignHCenter));
        QCOMPARE(document->findBlockByNumber(2).blockFormat().alignment(), Qt::Alignment(Qt::AlignLeft));
    }

    void dropsPendingActionWhenSaveFails() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QVERIFY(loadEditor(engine, backend, window));

        QTemporaryDir tempDir;
        window->setProperty("pendingAction", QStringLiteral("close"));
        backend.saveAs(QUrl::fromLocalFile(tempDir.filePath(QStringLiteral("missing/draft.md"))));

        QCOMPARE(window->property("pendingAction").toString(), QString());
    }

    void hiddenRangesIgnoreCodeBlocks() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        const QString sample = QStringLiteral(
            "Normal line with **bold**\n"
            "```\n"
            "# not heading\n"
            "code with *italic* and [link](url)\n"
            "```\n");
        editor->setProperty("text", sample);

        QCOMPARE(backend.hiddenRangesAt(20).size(), 2);
        QCOMPARE(backend.hiddenRangesAt(sample.indexOf(QStringLiteral("# not heading"))).size(), 0);
        QCOMPARE(backend.hiddenRangesAt(sample.indexOf(QStringLiteral("*italic*"))).size(), 0);
    }

    void smartReturnHandlesCodeBlocks() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("```\nline 1"));
        editor->setProperty("cursorPosition", 10);
        QVERIFY(QMetaObject::invokeMethod(editor, "smartReturn", Q_ARG(QVariant, false)));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("```\nline 1\n"));

        editor->setProperty("text", QStringLiteral("```print(1)```\nNormal paragraph"));
        editor->setProperty("cursorPosition", editor->property("text").toString().length());
        QVERIFY(QMetaObject::invokeMethod(editor, "smartReturn", Q_ARG(QVariant, false)));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("```print(1)```\nNormal paragraph\n\n"));
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        writeFile(path, readFile(path).toUtf8());
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        writeFile(path, "changed elsewhere");
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void scalesTextWithDesktopTextSize() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(saveDirectory.filePath(QStringLiteral("first.md"))));

        const QFileInfo suggested(Backend().suggestedSavePath());
        QCOMPARE(suggested.absolutePath(), saveDirectory.path());
        QCOMPARE(suggested.fileName(), QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        QCOMPARE(QFileInfo(Backend().suggestedSavePath()).absolutePath(), QDir::homePath());
    }

    void parsesJsonRpcFrames() {
        const QJsonObject msg1{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), 1},
            {QStringLiteral("method"), QStringLiteral("test")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("word"), QStringLiteral("ошыбкой")}}}
        };
        const QJsonObject msg2{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/publishDiagnostics")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("count"), 2}}}
        };

        const QByteArray wire1 = LspClient::encodeMessage(msg1);
        const QByteArray wire2 = LspClient::encodeMessage(msg2);
        const QByteArray combined = wire1 + wire2;

        QByteArray buffer;
        // Split across header, UTF-8 multibyte boundary, and concatenated second message.
        const int split1 = 10;
        const int split2 = wire1.size() - 5;
        const int split3 = wire1.size() + 14;

        buffer.append(combined.left(split1));
        QCOMPARE(LspClient::parseMessages(buffer).size(), 0);

        buffer.append(combined.mid(split1, split2 - split1));
        QCOMPARE(LspClient::parseMessages(buffer).size(), 0);

        buffer.append(combined.mid(split2, split3 - split2));
        QList<QJsonObject> firstBatch = LspClient::parseMessages(buffer);
        QCOMPARE(firstBatch.size(), 1);
        QCOMPARE(firstBatch.at(0).value(QStringLiteral("params")).toObject()
                     .value(QStringLiteral("word")).toString(),
                 QStringLiteral("ошыбкой"));
        QVERIFY(!buffer.isEmpty());

        buffer.append(combined.mid(split3));
        QList<QJsonObject> secondBatch = LspClient::parseMessages(buffer);
        QCOMPARE(secondBatch.size(), 1);
        QCOMPARE(secondBatch.at(0).value(QStringLiteral("method")).toString(),
                 QStringLiteral("textDocument/publishDiagnostics"));
        QVERIFY(buffer.isEmpty());

        // Two concatenated frames in one chunk
        buffer = combined;
        const QList<QJsonObject> both = LspClient::parseMessages(buffer);
        QCOMPARE(both.size(), 2);
        QVERIFY(buffer.isEmpty());
    }

    void highlightsSpellAndGrammarDiagnostics() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral(
            "---\n"
            "title: Frontmatter typo\n"
            "---\n"
            "**ошыбка** `inline_typo` https://exmple.com/path\n"
            "Привет мир grammar.\n"
            "```python\n"
            "code_tyop = 1\n"
            "```\n"));
        MarkdownHighlighter highlighter(&doc);

        const auto formatRangeAt = [](const QTextBlock &block, int localPos, QTextCharFormat &out) {
            const QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
            for (const QTextLayout::FormatRange &r : ranges) {
                if (localPos >= r.start && localPos < r.start + r.length) {
                    out = r.format;
                    return true;
                }
            }
            return false;
        };

        QHash<int, QList<MarkdownHighlighter::DiagnosticSpan>> diags;
        // Frontmatter line 1 (should be ignored)
        diags[1].append({7, 11, MarkdownHighlighter::DiagnosticSeverity::Spelling});
        // Line 3: **ошыбка** `inline_typo` https://exmple.com/path
        diags[3].append({0, 10, MarkdownHighlighter::DiagnosticSeverity::Spelling});
        diags[3].append({12, 11, MarkdownHighlighter::DiagnosticSeverity::Spelling});
        diags[3].append({25, 18, MarkdownHighlighter::DiagnosticSeverity::Spelling});
        // Line 4: Привет мир grammar.
        diags[4].append({0, 10, MarkdownHighlighter::DiagnosticSeverity::Grammar});
        // Line 6 (inside code block, should be ignored)
        diags[6].append({0, 9, MarkdownHighlighter::DiagnosticSeverity::Spelling});

        highlighter.setDiagnostics(diags);

        QTextCharFormat fmt;
        // Frontmatter must not be underlined
        QVERIFY(!formatRangeAt(doc.findBlockByNumber(1), 10, fmt)
                || fmt.underlineStyle() != QTextCharFormat::WaveUnderline);

        // Hidden ** marker at pos 0 must not be underlined, inner word at pos 3 must be bold + coral/red wave
        QVERIFY(formatRangeAt(doc.findBlockByNumber(3), 0, fmt));
        QVERIFY(fmt.underlineStyle() != QTextCharFormat::WaveUnderline);

        QVERIFY(formatRangeAt(doc.findBlockByNumber(3), 3, fmt));
        QCOMPARE(fmt.underlineStyle(), QTextCharFormat::WaveUnderline);
        QCOMPARE(fmt.fontWeight(), QFont::Bold);
        const QColor spellColor = fmt.underlineColor();

        // Inline code at pos 14 must not be underlined
        QVERIFY(formatRangeAt(doc.findBlockByNumber(3), 14, fmt));
        QVERIFY(fmt.underlineStyle() != QTextCharFormat::WaveUnderline);

        // Bare URL at pos 28 must not be underlined
        QVERIFY(!formatRangeAt(doc.findBlockByNumber(3), 28, fmt)
                || fmt.underlineStyle() != QTextCharFormat::WaveUnderline);

        // Grammar hint on line 4 must have WaveUnderline with a distinct amber color
        QVERIFY(formatRangeAt(doc.findBlockByNumber(4), 2, fmt));
        QCOMPARE(fmt.underlineStyle(), QTextCharFormat::WaveUnderline);
        QVERIFY(fmt.underlineColor() != spellColor);

        // Code block line 6 must not be underlined
        QVERIFY(formatRangeAt(doc.findBlockByNumber(6), 2, fmt));
        QVERIFY(fmt.underlineStyle() != QTextCharFormat::WaveUnderline);
    }

    void mapsDiagnosticsAndExposesSuggestions() {
        Backend backend;
        QQmlEngine engine;
        QScopedPointer<QObject> window;
        QObject *editor = loadEditor(engine, backend, window);
        QVERIFY(editor);

        const QString sample = QStringLiteral(
            "Привет ошыбка здесь\n"
            "Second line `ignored_code`\n"
            "Третья строка с опечаткой.\n");
        editor->setProperty("text", sample);

        LspClient::SpellDiagnostic d0;
        d0.startLine = 0;
        d0.startCharacter = 7;
        d0.endLine = 0;
        d0.endCharacter = 13;
        d0.code = QStringLiteral("MORFOLOGIK_RULE_RU_RU");
        d0.message = QStringLiteral("Возможно найдена орфографическая ошибка.");
        d0.language = QStringLiteral("ru-RU");
        d0.suggestions = {QStringLiteral("ошибка")};

        LspClient::SpellDiagnostic dIgnored;
        dIgnored.startLine = 1;
        dIgnored.startCharacter = 13;
        dIgnored.endLine = 1;
        dIgnored.endCharacter = 25;
        dIgnored.code = QStringLiteral("MORFOLOGIK_RULE_EN_US");
        dIgnored.message = QStringLiteral("Possible spelling mistake");

        LspClient::SpellDiagnostic d2;
        d2.startLine = 2;
        d2.startCharacter = 16;
        d2.endLine = 2;
        d2.endCharacter = 25;
        d2.code = QStringLiteral("MORFOLOGIK_RULE_RU_RU");
        d2.message = QStringLiteral("Орфографическая ошибка");
        d2.language = QStringLiteral("ru-RU");
        d2.suggestions = {QStringLiteral("опечаткой"), QStringLiteral("отпечаткой")};

        backend.setSpellDiagnostics({d0, dIgnored, d2});
        QCOMPARE(backend.spellUnderlineSpans().size(), 2);

        // Inline code diagnostic should have been filtered out
        const int codePos = sample.indexOf(QStringLiteral("ignored_code")) + 2;
        QCOMPARE(backend.diagnosticAt(codePos).value(QStringLiteral("valid")).toBool(), false);

        // First diagnostic should resolve at cursor inside word
        const int word0Pos = sample.indexOf(QStringLiteral("ошыбка")) + 3;
        const QVariantMap info0 = backend.diagnosticAt(word0Pos);
        QCOMPARE(info0.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(info0.value(QStringLiteral("word")).toString(), QStringLiteral("ошыбка"));
        QCOMPARE(info0.value(QStringLiteral("suggestions")).toStringList(),
                 QStringList{QStringLiteral("ошибка")});
        QCOMPARE(info0.value(QStringLiteral("isSpelling")).toBool(), true);

        // Open SpellPopup via keyboard trigger and choose first suggestion
        editor->setProperty("cursorPosition", word0Pos);
        QObject *popup = window->findChild<QObject *>(QStringLiteral("spellPopup"));
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(editor, "openSpellPopupAtCursor"));
        QCOMPARE(popup->property("opened").toBool(), true);
        QVERIFY(QMetaObject::invokeMethod(popup, "activateIndex", Q_ARG(QVariant, 0)));
        QCOMPARE(popup->property("opened").toBool(), false);

        QVERIFY(editor->property("text").toString().startsWith(QStringLiteral("Привет ошибка здесь")));
        // Diagnostic on line 0 was cleared immediately on edit, while line 2 stays valid
        QCOMPARE(backend.diagnosticAt(word0Pos).value(QStringLiteral("valid")).toBool(), false);
        const int word2Pos = editor->property("text").toString().indexOf(QStringLiteral("опечаткой")) + 2;
        QCOMPARE(backend.diagnosticAt(word2Pos).value(QStringLiteral("valid")).toBool(), true);

        // Undo restores original text (remove + insert = 2 QTextEdit commands in replaceRange)
        QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
        QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
        QCOMPARE(editor->property("text").toString(), sample);

        // Add to dictionary persists and clears matching diagnostics
        backend.setSpellDiagnostics({d0, d2});
        backend.addToDictionary(QStringLiteral("ошыбка"), QStringLiteral("ru-RU"));
        QCOMPARE(backend.diagnosticAt(sample.indexOf(QStringLiteral("ошыбка")) + 1)
                     .value(QStringLiteral("valid")).toBool(),
                 false);
        QSettings settings;
        settings.beginGroup(QStringLiteral("spellcheck/dictionary"));
        QVERIFY(settings.value(QStringLiteral("ru-RU")).toStringList().contains(QStringLiteral("ошыбка")));
        settings.endGroup();
    }

private:
    QTemporaryDir m_settingsDirectory;
    SystemTheme m_systemTheme;
};

QTEST_MAIN(WriterTest)
#include "tst_writer.moc"
