#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextDocument>

#include "backend.h"
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

private:
    QTemporaryDir m_settingsDirectory;
    SystemTheme m_systemTheme;
};

QTEST_MAIN(WriterTest)
#include "tst_writer.moc"
