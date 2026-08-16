.pragma library

function normalizePlainText(text) {
    return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function replaceRange(editor, rangeStart, rangeEnd, replacement,
                      selectionStartOffset, selectionEndOffset) {
    var start = Math.max(0, Math.min(editor.text.length, rangeStart));
    var end = Math.max(start, Math.min(editor.text.length, rangeEnd));
    var insertedText = normalizePlainText(replacement);

    if (start !== end)
        editor.remove(start, end);

    editor.cursorPosition = start;
    editor.insert(start, insertedText);

    // TextEdit.insert() already leaves the caret after the inserted text. Only
    // move it again when the caller deliberately requests a selection/caret
    // within the replacement.
    if (selectionStartOffset !== undefined && selectionEndOffset !== undefined) {
        var insertedEnd = editor.cursorPosition;
        var selectionStart = Math.max(start,
                                      Math.min(insertedEnd, start + selectionStartOffset));
        var selectionEnd = Math.max(start,
                                    Math.min(insertedEnd, start + selectionEndOffset));
        if (selectionStart === selectionEnd)
            editor.cursorPosition = selectionStart;
        else
            editor.select(selectionStart, selectionEnd);
    }

    return insertedText;
}

function stripMarkdownFormatting(text) {
    var s = text;
    s = s.replace(/(\*\*|__|\*\*\*|___)(.*?)\1/g, "$2");
    s = s.replace(/(^|[^\\])(\*|_)(.*?)\2/g, "$1$3");
    s = s.replace(/\[([^\]]*)\]\([^)]*\)/g, "$1");
    return s;
}

function toggleHeading(editor) {
    var text = editor.text;
    var pos = editor.cursorPosition;
    var start = text.lastIndexOf('\n', pos - 1) + 1;
    var end = text.indexOf('\n', pos);
    if (end === -1) end = text.length;
    
    var lineText = text.substring(start, end);
    var match = lineText.match(/^(#{1,6})(\s+)(.*)$/);
    var distFromEnd = end - pos;
    var newText = "";
    
    if (match) {
        var level = match[1].length;
        var stripped = stripMarkdownFormatting(match[3]);
        if (level < 6) {
            newText = "#" + match[1] + match[2] + stripped;
        } else {
            newText = "# " + stripped;
        }
    } else {
        var trimmed = lineText.replace(/^\s+/, '');
        newText = "# " + stripMarkdownFormatting(trimmed);
    }
    
    var newCursorOffset = newText.length - distFromEnd;
    if (newCursorOffset < 0) newCursorOffset = 0;
    replaceRange(editor, start, end, newText, newCursorOffset, newCursorOffset);
}

function clearHeading(editor) {
    var text = editor.text;
    var pos = editor.cursorPosition;
    var start = text.lastIndexOf('\n', pos - 1) + 1;
    var end = text.indexOf('\n', pos);
    if (end === -1) end = text.length;
    
    var lineText = text.substring(start, end);
    var match = lineText.match(/^(#{1,6})(\s+)(.*)$/);
    if (match) {
        var distFromEnd = end - pos;
        var newText = match[3];
        var newCursorOffset = newText.length - distFromEnd;
        if (newCursorOffset < 0) newCursorOffset = 0;
        replaceRange(editor, start, end, newText, newCursorOffset, newCursorOffset);
    }
}
