#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "markdownhighlighter.h"

class WriterTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
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

        // Dashes and underscores are no longer horizontal rules
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

        QTextBlock b0 = doc.findBlockByNumber(0);
        QTextBlock b1 = doc.findBlockByNumber(1);
        QTextBlock b2 = doc.findBlockByNumber(2);
        QTextBlock b3 = doc.findBlockByNumber(3);
        QTextBlock b4 = doc.findBlockByNumber(4);
        QTextBlock b5 = doc.findBlockByNumber(5);

        QCOMPARE(b0.userState(), int(MarkdownHighlighter::StateInFrontmatter));
        QCOMPARE(b1.userState(), int(MarkdownHighlighter::StateInFrontmatter));
        QCOMPARE(b2.userState(), int(MarkdownHighlighter::StateInFrontmatter));
        QCOMPARE(b3.userState(), int(MarkdownHighlighter::StateInFrontmatter));
        QCOMPARE(b4.userState(), int(MarkdownHighlighter::StateNormal));
        QCOMPARE(b5.userState(), int(MarkdownHighlighter::StateNormal));
    }

    void parsesAndUpdatesMetadata() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("# My Story\nHello world"));
        QVariantMap meta = backend.documentMetadata();
        QCOMPARE(meta.value("title").toString(), QStringLiteral("My Story"));
        QVERIFY(!meta.value("created").toString().isEmpty());

        backend.updateDocumentMetadata(QStringLiteral("New Title"), QStringLiteral("Author X"), QStringLiteral("Fiction"));
        QVariantMap updatedMeta = backend.documentMetadata();
        QCOMPARE(updatedMeta.value("title").toString(), QStringLiteral("New Title"));
        QCOMPARE(updatedMeta.value("author").toString(), QStringLiteral("Author X"));
        QCOMPARE(updatedMeta.value("topic").toString(), QStringLiteral("Fiction"));
        QCOMPARE(backend.defaultAuthor(), QStringLiteral("Author X"));

        // Editor text is clean body text (no frontmatter clutter in editor)
        QString text = editor->property("text").toString();
        QCOMPARE(text, QStringLiteral("# My Story\nHello world"));

        // When saved to disk, frontmatter is written
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("Story.md"));
        backend.saveAs(QUrl::fromLocalFile(filePath));

        QFile savedFile(filePath);
        QVERIFY(savedFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QString savedContent = QString::fromUtf8(savedFile.readAll());
        QVERIFY(savedContent.startsWith(QStringLiteral("---\n")));
        QVERIFY(savedContent.contains(QStringLiteral("title: New Title")));
        QVERIFY(savedContent.contains(QStringLiteral("author: Author X")));
        QVERIFY(savedContent.contains(QStringLiteral("topic: Fiction")));
        QVERIFY(savedContent.contains(QStringLiteral("# My Story\nHello world")));
    }

    void autoInitializesAndSavesMetadata() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        backend.setDefaultAuthor(QStringLiteral("Auto Author"));
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // Editor text starts clean (empty)
        QString initialText = editor->property("text").toString();
        QCOMPARE(initialText, QString());

        // Metadata is initialized in memory
        QVariantMap meta = backend.documentMetadata();
        QCOMPARE(meta.value("author").toString(), QStringLiteral("Auto Author"));
        QVERIFY(!meta.value("created").toString().isEmpty());
        QVERIFY(!meta.value("updated").toString().isEmpty());

        QCOMPARE(backend.modified(), false);
        QCOMPARE(backend.wordCount(), 0);

        editor->setProperty("text", QStringLiteral("# Automatic Novel\nSome actual body content here."));
        QCOMPARE(backend.modified(), true);
        QTRY_COMPARE(backend.wordCount(), 7);

        QCOMPARE(Backend::suggestedFileName(editor->property("text").toString()), QStringLiteral("Automatic Novel.md"));

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("Automatic Novel.md"));
        backend.saveAs(QUrl::fromLocalFile(filePath));

        // File on disk has YAML frontmatter
        QFile savedFile(filePath);
        QVERIFY(savedFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QString savedContent = QString::fromUtf8(savedFile.readAll());
        QVERIFY(savedContent.contains(QStringLiteral("title: Automatic Novel")));
        QVERIFY(savedContent.contains(QStringLiteral("author: Auto Author")));
        QVERIFY(savedContent.contains(QStringLiteral("updated:")));

        // When reopening the file, editor text is clean body
        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Automatic Novel\nSome actual body content here."));
        QVariantMap reopenedMeta = backend.documentMetadata();
        QCOMPARE(reopenedMeta.value("title").toString(), QStringLiteral("Automatic Novel"));
        QCOMPARE(reopenedMeta.value("author").toString(), QStringLiteral("Auto Author"));
    }

    void preservesCustomFrontmatterAndRecovery() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("Custom.md"));

        QFile initFile(filePath);
        QVERIFY(initFile.open(QIODevice::WriteOnly | QIODevice::Text));
        initFile.write(
            "---\n"
            "title: Custom Fields\n"
            "author: Alice\n"
            "category: Space Opera\n"
            "draft: true\n"
            "---\n\n"
            "# Chapter 1\n"
            "Deep space silence.\n"
        );
        initFile.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        backend.open(QUrl::fromLocalFile(filePath));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Chapter 1\nDeep space silence.\n"));

        // Save and verify custom fields are preserved
        backend.save();
        QFile savedFile(filePath);
        QVERIFY(savedFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QString savedContent = QString::fromUtf8(savedFile.readAll());
        QVERIFY(savedContent.contains(QStringLiteral("category: Space Opera")));
        QVERIFY(savedContent.contains(QStringLiteral("draft: true")));
    }

    void handlesCrlfLineEndings() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("CRLF.md"));

        QFile initFile(filePath);
        QVERIFY(initFile.open(QIODevice::WriteOnly));
        initFile.write(
            "---\r\n"
            "title: Windows Doc\r\n"
            "author: Bob\r\n"
            "---\r\n\r\n"
            "# Windows Chapter\r\n"
            "Text with CRLF\r\n"
        );
        initFile.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        backend.open(QUrl::fromLocalFile(filePath));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Windows Chapter\nText with CRLF\n"));
        QCOMPARE(backend.modified(), false);
    }

    void tracksMetadataModifications() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString filePath = tempDir.filePath(QStringLiteral("Track.md"));

        QFile initFile(filePath);
        QVERIFY(initFile.open(QIODevice::WriteOnly | QIODevice::Text));
        initFile.write(
            "---\n"
            "title: Original Title\n"
            "author: Alice\n"
            "---\n\n"
            "# Original Heading\n"
            "Body content.\n"
        );
        initFile.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        backend.open(QUrl::fromLocalFile(filePath));
        QCOMPARE(backend.modified(), false);

        // Modifying metadata sets modified = true
        backend.updateDocumentMetadata(QStringLiteral("Altered Title"), QStringLiteral("Alice"), QString());
        QCOMPARE(backend.modified(), true);

        // Typing in editor and reverting text should NOT clear modified since metadata is still altered
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("# Original Heading\nBody content.\nExtra"));
        QCOMPARE(backend.modified(), true);
        editor->setProperty("text", QStringLiteral("# Original Heading\nBody content.\n"));
        QCOMPARE(backend.modified(), true);

        // Reverting metadata to original restores unmodified state
        backend.updateDocumentMetadata(QStringLiteral("Original Title"), QStringLiteral("Alice"), QString());
        QCOMPARE(backend.modified(), false);
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

        QTextBlock b0 = doc.findBlockByNumber(0);
        QTextBlock b1 = doc.findBlockByNumber(1);
        QTextBlock b2 = doc.findBlockByNumber(2);
        QTextBlock b3 = doc.findBlockByNumber(3);
        QTextBlock b4 = doc.findBlockByNumber(4);
        QTextBlock b5 = doc.findBlockByNumber(5);

        QCOMPARE(b0.userState(), int(MarkdownHighlighter::StateNormal));
        QCOMPARE(b1.userState(), int(MarkdownHighlighter::StateInCodeBlock));
        QCOMPARE(b2.userState(), int(MarkdownHighlighter::StateInCodeBlock));
        QCOMPARE(b3.userState(), int(MarkdownHighlighter::StateInCodeBlock));
        QCOMPARE(b4.userState(), int(MarkdownHighlighter::StateNormal));
        QCOMPARE(b5.userState(), int(MarkdownHighlighter::StateNormal));
    }

    void hiddenRangesIgnoreCodeBlocks() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        const QString sample = QStringLiteral(
            "Normal line with **bold**\n"
            "```\n"
            "# not heading\n"
            "code with *italic* and [link](url)\n"
            "```\n");
        editor->setProperty("text", sample);

        // Position on first line with **bold** (pos 20) has hidden markers
        QVariantList normalRanges = backend.hiddenRangesAt(20);
        QCOMPARE(normalRanges.size(), 2);

        // Position inside code block (pos 35 on "# not heading") has NO hidden ranges
        int codePos = sample.indexOf(QStringLiteral("# not heading"));
        QVariantList codeRanges1 = backend.hiddenRangesAt(codePos);
        QCOMPARE(codeRanges1.size(), 0);

        // Position inside code block on "code with *italic*" has NO hidden ranges
        int codePos2 = sample.indexOf(QStringLiteral("*italic*"));
        QVariantList codeRanges2 = backend.hiddenRangesAt(codePos2);
        QCOMPARE(codeRanges2.size(), 0);
    }

    void smartReturnHandlesCodeBlocks() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // Case 1: Inside multi-line code block -> single \n inserted
        editor->setProperty("text", QStringLiteral("```\nline 1"));
        editor->setProperty("cursorPosition", 10);
        QVERIFY(QMetaObject::invokeMethod(editor, "smartReturn", Q_ARG(QVariant, false)));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("```\nline 1\n"));

        // Case 2: After single-line code block -> normal paragraph double \n inserted
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

        QFile readFile(path);
        QVERIFY(readFile.open(QIODevice::ReadOnly));
        const QByteArray savedBytes = readFile.readAll();
        readFile.close();

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.write(savedBytes);
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
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

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveAsRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QObject *openDialog = window->findChild<QObject *>(QStringLiteral("openDialog"));
        if (openDialog) {
            QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
            QCOMPARE(openDialog->property("visible").toBool(), true);
        } else {
            QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        }
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `linux display text size 16` sets the GNOME factor to 16/12.
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

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        const QUrl suggestedUrl = nextDocument.suggestedSaveUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        const QUrl fallbackUrl = fallbackDocument.suggestedSaveUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(WriterTest)
#include "tst_writer.moc"
