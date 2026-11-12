#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

void MarkdownHighlighter::setDiagnostics(const QHash<int, QList<DiagnosticSpan>> &diagnostics) {
    if (m_diagnostics == diagnostics)
        return;

    QSet<int> changedBlocks;
    for (auto it = m_diagnostics.cbegin(); it != m_diagnostics.cend(); ++it) {
        if (!diagnostics.contains(it.key()) || diagnostics.value(it.key()) != it.value())
            changedBlocks.insert(it.key());
    }
    for (auto it = diagnostics.cbegin(); it != diagnostics.cend(); ++it) {
        if (!m_diagnostics.contains(it.key()) || m_diagnostics.value(it.key()) != it.value())
            changedBlocks.insert(it.key());
    }

    m_diagnostics = diagnostics;

    if (!document() || changedBlocks.isEmpty())
        return;

    QList<int> sorted = changedBlocks.values();
    std::sort(sorted.begin(), sorted.end());
    for (int blockNum : sorted) {
        const QTextBlock block = document()->findBlockByNumber(blockNum);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::clearDiagnosticsInRange(int startBlock, int endBlock) {
    if (m_diagnostics.isEmpty() || !document())
        return;

    QList<int> cleared;
    for (int blockNum = startBlock; blockNum <= endBlock; ++blockNum) {
        if (m_diagnostics.remove(blockNum) > 0)
            cleared.append(blockNum);
    }

    for (int blockNum : cleared) {
        const QTextBlock block = document()->findBlockByNumber(blockNum);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    m_ruleFormat = QTextCharFormat();
    m_ruleFormat.setForeground(marker);
    m_ruleFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_ruleFormat.setFontLetterSpacing(20.0);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = document() ? document()->defaultFont() : QFont();
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    m_headingFormat = QTextCharFormat();
    m_headingFormat.setForeground(text);
    m_headingFormat.setFontWeight(QFont::Bold);

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_boldItalicFormat = QTextCharFormat();
    m_boldItalicFormat.setFontWeight(QFont::Bold);
    m_boldItalicFormat.setFontItalic(true);
    m_boldItalicFormat.setForeground(text);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);

    m_codeBlockFormat = QTextCharFormat();
    m_codeBlockFormat.setForeground(text);
    m_codeBlockFormat.setBackground(codeBackground);

    m_codeFenceFormat = QTextCharFormat();
    m_codeFenceFormat.setForeground(marker);
    m_codeFenceFormat.setBackground(codeBackground);

    m_frontmatterFenceFormat = QTextCharFormat();
    m_frontmatterFenceFormat.setForeground(marker);

    m_frontmatterKeyFormat = QTextCharFormat();
    m_frontmatterKeyFormat.setForeground(m_darkMode ? QColor(QStringLiteral("#8ab4f8"))
                                                    : QColor(QStringLiteral("#1a73e8")));
    m_frontmatterKeyFormat.setFontWeight(QFont::Bold);

    m_frontmatterValueFormat = QTextCharFormat();
    m_frontmatterValueFormat.setForeground(text);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));

    m_spellingUnderlineColor = m_darkMode ? QColor(QStringLiteral("#e05d5d"))
                                          : QColor(QStringLiteral("#d32f2f"));
    m_grammarUnderlineColor = m_darkMode ? QColor(QStringLiteral("#e0a84b"))
                                         : QColor(QStringLiteral("#c77c02"));
}

bool MarkdownHighlighter::isRuleLine(const QString &text) {
    static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}\\*(?:\\s*\\*){2,}\\s*$"));
    return ruleRe.match(text).hasMatch();
}

bool MarkdownHighlighter::isFenceLine(const QString &text, QString *delimiter, QString *info) {
    static const QRegularExpression fenceRe(QStringLiteral("^\\s{0,3}(`{3,}|~{3,})(.*)$"));
    const QRegularExpressionMatch match = fenceRe.match(text);
    if (!match.hasMatch())
        return false;
    if (delimiter)
        *delimiter = match.captured(1);
    if (info)
        *info = match.captured(2).trimmed();
    return true;
}

bool MarkdownHighlighter::isClosingFence(const QString &text, const QString &delimiter) {
    static const QRegularExpression closingFenceRe(QStringLiteral("^\\s{0,3}(`{3,}|~{3,})\\s*$"));
    const QRegularExpressionMatch match = closingFenceRe.match(text);
    if (!match.hasMatch())
        return false;
    if (!delimiter.isEmpty()) {
        const QString delim = match.captured(1);
        if (delim.at(0) != delimiter.at(0) || delim.length() < delimiter.length())
            return false;
    }
    return true;
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    int prevState = previousBlockState();
    if (prevState < 0)
        prevState = StateNormal;

    const int blockNum = currentBlock().blockNumber();

    if (blockNum == 0 && text.trimmed() == QStringLiteral("---")) {
        setCurrentBlockState(StateInFrontmatter);
        setFormat(0, text.length(), m_frontmatterFenceFormat);
        highlightSearch(text);
        return;
    }

    if (prevState == StateInFrontmatter) {
        if (text.trimmed() == QStringLiteral("---") || text.trimmed() == QStringLiteral("...")) {
            setCurrentBlockState(StateNormal);
            setFormat(0, text.length(), m_frontmatterFenceFormat);
        } else {
            setCurrentBlockState(StateInFrontmatter);
            int colon = text.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                setFormat(0, colon + 1, m_frontmatterKeyFormat);
                if (colon + 1 < text.length()) {
                    setFormat(colon + 1, text.length() - (colon + 1), m_frontmatterValueFormat);
                }
            } else {
                setFormat(0, text.length(), m_frontmatterValueFormat);
            }
        }
        highlightSearch(text);
        return;
    }

    if (prevState == StateInCodeBlock) {
        if (isClosingFence(text)) {
            setCurrentBlockState(StateNormal);
            setFormat(0, text.length(), m_codeFenceFormat);
        } else {
            setCurrentBlockState(StateInCodeBlock);
            setFormat(0, text.length(), m_codeBlockFormat);
        }
    } else {
        QString delim;
        QString info;
        if (isFenceLine(text, &delim, &info)) {
            // Check if closed on same line (e.g. ```code```)
            int closingIdx = info.indexOf(delim);
            if (closingIdx >= 0 && info.mid(closingIdx).trimmed() == delim) {
                setCurrentBlockState(StateNormal);
                setFormat(0, text.length(), m_codeBlockFormat);
            } else {
                setCurrentBlockState(StateInCodeBlock);
                setFormat(0, text.length(), m_codeFenceFormat);
                if (!info.isEmpty()) {
                    int infoStart = text.indexOf(info);
                    if (infoStart >= 0) {
                        setFormat(infoStart, info.length(), m_codeFormat);
                    }
                }
            }
        } else {
            setCurrentBlockState(StateNormal);
            if (!text.isEmpty()) {
                bool skipInline = highlightMarkers(text);
                if (!skipInline && (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
                    || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('[')))) {
                    highlightInline(text);
                }
            }
        }
    }

    highlightSearch(text);
    if (currentBlockState() == StateNormal && !isFenceLine(text) && !isRuleLine(text))
        highlightDiagnostics(text);
}

void MarkdownHighlighter::highlightDiagnostics(const QString &text) {
    if (text.isEmpty() || m_diagnostics.isEmpty())
        return;

    const QList<DiagnosticSpan> spans = m_diagnostics.value(currentBlock().blockNumber());
    if (spans.isEmpty())
        return;

    const QList<Span> ignored = ignoredSpellSpans(text);
    const auto isIgnoredPos = [&ignored](int pos) {
        for (const Span &span : ignored) {
            if (pos >= span.start && pos < span.start + span.length)
                return true;
        }
        return false;
    };

    for (const DiagnosticSpan &diag : spans) {
        const int start = qBound(0, diag.start, text.length());
        const int end = qBound(start, diag.start + diag.length, text.length());
        if (start >= end)
            continue;

        const QColor underlineColor = diag.severity == DiagnosticSeverity::Spelling
            ? m_spellingUnderlineColor
            : m_grammarUnderlineColor;

        int pos = start;
        while (pos < end) {
            if (isIgnoredPos(pos)) {
                ++pos;
                continue;
            }
            QTextCharFormat base = format(pos);
            int runEnd = pos + 1;
            while (runEnd < end && !isIgnoredPos(runEnd) && format(runEnd) == base)
                ++runEnd;

            base.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            base.setUnderlineColor(underlineColor);
            setFormat(pos, runEnd - pos, base);
            pos = runEnd;
        }
    }
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

bool MarkdownHighlighter::highlightMarkers(const QString &text) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return false;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            int level = heading.capturedLength(1); // 1 to 6
            qreal baseSize = document() ? document()->defaultFont().pointSizeF() : 12.0;
            if (baseSize <= 0) {
                baseSize = document() ? document()->defaultFont().pixelSize() : 12.0;
            }
            
            QTextCharFormat fmt = m_headingFormat;
            qreal scale = 1.0 + (6.0 - level) * 0.2; // 2.0 for # down to 1.0 for ######
            if (document() && document()->defaultFont().pointSizeF() > 0) {
                fmt.setFontPointSize(baseSize * scale);
            } else {
                fmt.setProperty(QTextFormat::FontPixelSize, baseSize * scale);
            }

            setFormat(0, heading.capturedLength(1) + heading.capturedLength(2),
                      m_hiddenMarkerFormat);
            setFormat(heading.capturedStart(3), heading.capturedLength(3),
                      fmt);
            return false;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), m_markerFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch())
            setFormat(0, list.capturedLength(1), m_markerFormat);
    }

    if (firstChar == QLatin1Char('*')) {
        if (isRuleLine(text)) {
            setFormat(0, text.length(), m_ruleFormat);
            return true;
        }
    }
    return false;
}

void MarkdownHighlighter::highlightInline(const QString &text) {
    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.content.length == 0) continue;
        QTextCharFormat contentFormat =
            item.kind == InlineKind::BoldItalic ? m_boldItalicFormat
            : item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
                                              : m_linkFormat;
        QTextCharFormat existingFormat = format(item.content.start);
        existingFormat.merge(contentFormat);
        setFormat(item.content.start, item.content.length, existingFormat);
        for (const Span &marker : item.markers)
            setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
    }

    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression codeRe(QStringLiteral("(`+)(.+?)\\1"));
        QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
        while (codeMatches.hasNext()) {
            const QRegularExpressionMatch match = codeMatches.next();
            QTextCharFormat fmt = format(match.capturedStart(0));
            fmt.merge(m_codeFormat);
            setFormat(match.capturedStart(0), match.capturedLength(0), fmt);
        }
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (isRuleLine(text)) {
        return markup;
    }
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('['))) {
        return markup;
    }

    struct CodeSpan {
        int start;
        int end;
    };
    QList<CodeSpan> codeSpans;
    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression inlineCodeRe(QStringLiteral("(`+)(.+?)\\1"));
        QRegularExpressionMatchIterator it = inlineCodeRe.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            codeSpans.append({int(m.capturedStart(0)), int(m.capturedEnd(0))});
        }
    }

    const auto overlapsCode = [&codeSpans](int start, int end) {
        for (const auto &cs : codeSpans) {
            if ((start >= cs.start && start < cs.end) || (end > cs.start && end <= cs.end))
                return true;
        }
        return false;
    };

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    static const QRegularExpression boldItalicRe(QStringLiteral("(\\*\\*\\*|___)(.*?)(\\1)"));
    QRegularExpressionMatchIterator boldItalicMatches = boldItalicRe.globalMatch(text);
    while (boldItalicMatches.hasNext()) {
        const QRegularExpressionMatch match = boldItalicMatches.next();
        if (!overlapsCode(match.capturedStart(0), match.capturedEnd(0))) {
            markup.append({InlineKind::BoldItalic, span(match, 2),
                           {span(match, 1), span(match, 3)}});
        }
    }

    static const QRegularExpression boldRe(QStringLiteral("(?<!\\*)(\\*\\*|__)(?!\\*)(.*?)(?<!\\*)(\\1)(?!\\*)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        if (!overlapsCode(match.capturedStart(0), match.capturedEnd(0))) {
            markup.append({InlineKind::Bold, span(match, 2),
                           {span(match, 1), span(match, 3)}});
        }
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        if (!overlapsCode(match.capturedStart(0), match.capturedEnd(0))) {
            const Span whole = span(match, 0);
            const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
            markup.append({InlineKind::Italic, span(match, contentIndex),
                           {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
        }
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]*)\\]\\(((?:\\\\.|[^)])*)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        if (!overlapsCode(match.capturedStart(0), match.capturedEnd(0))) {
            const Span whole = span(match, 0);
            const Span content = span(match, 1);
            const int contentEnd = content.start + content.length;
            markup.append({InlineKind::Link, content,
                           {{whole.start, 1},
                            {contentEnd, whole.start + whole.length - contentEnd}}});
        }
    }

    return markup;
}

QList<MarkdownHighlighter::Span> MarkdownHighlighter::ignoredSpellSpans(const QString &text) {
    QList<Span> spans;
    if (text.isEmpty())
        return spans;

    if (text.startsWith(QLatin1Char('#'))) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6}\\s+)"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch())
            spans.append({0, int(heading.capturedLength(1))});
    }

    if (text.contains(QLatin1Char('`'))) {
        static const QRegularExpression inlineCodeRe(QStringLiteral("(`+)(.+?)\\1"));
        QRegularExpressionMatchIterator it = inlineCodeRe.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            spans.append({int(m.capturedStart(0)), int(m.capturedLength(0))});
        }
    }

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.content.length == 0)
            continue;
        for (const Span &marker : item.markers) {
            if (marker.length > 0)
                spans.append(marker);
        }
    }

    static const QRegularExpression urlRe(
        QStringLiteral("\\b(?:(?:https?|ftp)://|mailto:|www\\.)[^\\s)>\\]]+"));
    QRegularExpressionMatchIterator urlIt = urlRe.globalMatch(text);
    while (urlIt.hasNext()) {
        const QRegularExpressionMatch m = urlIt.next();
        spans.append({int(m.capturedStart(0)), int(m.capturedLength(0))});
    }

    return spans;
}

bool MarkdownHighlighter::isRangeIgnoredForSpelling(const QTextBlock &block, int localStart,
                                                    int localLength) {
    if (!block.isValid() || localLength <= 0)
        return true;

    const QString text = block.text();
    const int state = block.userState();
    if (state == StateInCodeBlock || state == StateInFrontmatter
            || isFenceLine(text) || isRuleLine(text)
            || (block.blockNumber() == 0 && text.trimmed() == QLatin1String("---")))
        return true;

    const int start = qBound(0, localStart, text.length());
    const int end = qBound(start, localStart + localLength, text.length());
    if (start >= end)
        return true;

    const QList<Span> ignored = ignoredSpellSpans(text);
    for (int pos = start; pos < end; ++pos) {
        bool covered = false;
        for (const Span &span : ignored) {
            if (pos >= span.start && pos < span.start + span.length) {
                covered = true;
                break;
            }
        }
        if (!covered)
            return false;
    }
    return true;
}
