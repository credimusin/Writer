#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    enum BlockState {
        StateNormal = 0,
        StateInCodeBlock = 1,
        StateInFrontmatter = 2
    };

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, BoldItalic, Link };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers, and the editor uses it (via
    // Backend::hiddenRangesAt) to skip the caret over the hidden markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);

    // Helpers to check code fences and rules
    static bool isFenceLine(const QString &text, QString *delimiter = nullptr, QString *info = nullptr);
    static bool isClosingFence(const QString &text, const QString &delimiter = QString());
    static bool isRuleLine(const QString &text);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    bool highlightMarkers(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);

    bool m_darkMode = true;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_ruleFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_boldItalicFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_codeFenceFormat;
    QTextCharFormat m_frontmatterFenceFormat;
    QTextCharFormat m_frontmatterKeyFormat;
    QTextCharFormat m_frontmatterValueFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
