# Writer

A dead-simple Markdown writing app built with Qt Quick and C++.


## Build and Run

To build the project locally, ensure you have Qt 6 (`qt6-base`, `qt6-declarative`) and a C++ compiler installed. Then run the build script:

```sh
./bin/build
```

This will compile the application and place the executable at `build/writer` (`build/writer.app` on macOS). You can launch it directly:

```sh
./build/writer
```

### Installation

To install Writer system-wide (requires sudo privileges):

```sh
./bin/install
```

## Shortcuts

On macOS, `Ctrl` means `Cmd`.

- `Ctrl+S` saves the document.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file.
- `Ctrl+M` opens document properties / metadata dialog (title, author, topic).
- `Ctrl+P` exports the document to a WYSIWYG PDF (preserves editor layout and formatting).
- `Ctrl+N` opens a new Writer window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold (`**`), italic (`_`), and link (`[]()`) Markdown.
- `Ctrl+H` cycles through heading levels (H1 to H6).
- `Ctrl+Shift+H` clears the heading format.
- `Ctrl+PgUp` / `Ctrl+Home` and `Ctrl+PgDn` / `Ctrl+End` jump to the start or end of the document (add `Shift` to select).
- `Ctrl+T` switch the visual theme.
- `Ctrl+.` opens spell and grammar suggestions at the cursor (also accessible via right-click on an underlined word).
- `F7` toggles spell and grammar checking on the fly.
- `Ctrl+?` shows the keyboard shortcut reference.

### Document Properties (YAML Frontmatter)
Writer supports YAML frontmatter at the beginning of files for document metadata (`title`, `author`, `topic`, `created`, `updated`). Press `Ctrl+M` to open the properties dialog to view and edit metadata. The author is saved in settings across documents, and the document title is automatically suggested as the file name when saving.

### Markdown Syntax Hiding
Writer automatically hides markdown syntax characters (like asterisks, underscores, and link brackets) for a cleaner reading and writing experience. They seamlessly reappear when their inner content becomes empty, or you can manage formatting entirely through shortcuts.

### Spell & Grammar Checking
If `ltex-ls` (or `ltex-ls-plus`) is installed in `$PATH` (or `~/.local/bin`), Writer automatically runs offline Markdown-aware spell and grammar checking in the background (and stays silent with no UI clutter if no server is found). Spelling errors are highlighted with a coral/red wavy underline and grammar/style hints with an amber underline, while code blocks, inline code, URLs, and YAML frontmatter are ignored. Press `Ctrl+.` or right-click an underlined word to choose a replacement or add the word to your custom dictionary. Use `F7` or the bottom-right `Spell` / language (`RU` / `EN`) controls to toggle checking or switch languages on the fly.

Unsaved drafts are recovered after an abnormal exit. Writer also watches open files
and warns before an external change can replace local work.

On Linux, text follows GNOME's `text-scaling-factor` and re-flows without a restart.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`
- Linux: `xdg-desktop-portal` and a portal backend
- Optional: `ltex-ls` (or `ltex-ls-plus`) for offline Markdown spell and grammar checking


---

Works for me on Fedora Sway Atomic.
