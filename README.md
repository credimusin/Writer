# Writer

A dead-simple Markdown writing app built with Qt Quick and C++ that automatically follows system dark/light mode.




## Build and Run

To build the project locally, ensure you have Qt 6 (`qt6-base`, `qt6-declarative`) and a C++ compiler installed. Then run the build script:

```sh
./bin/build
```

This will compile the application and place the executable at `build/writer`. You can launch it directly:

```sh
./build/writer
```

### Installation

To install Writer system-wide (requires sudo privileges):

```sh
./bin/install
```

## Shortcuts

- `Ctrl+S` saves the document.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file.
- `Ctrl+N` opens a new Writer window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold (`**`), italic (`_`), and link (`[]()`) Markdown.
- `Ctrl+H` cycles through heading levels (H1 to H6).
- `Ctrl+Shift+H` clears the heading format.
- `Ctrl+?` shows the keyboard shortcut reference.

### Markdown Syntax Hiding
Writer automatically hides markdown syntax characters (like asterisks, underscores, and link brackets) for a cleaner reading and writing experience. They seamlessly reappear when their inner content becomes empty, or you can manage formatting entirely through shortcuts.

Unsaved drafts are recovered after an abnormal exit. Writer also watches open files
and warns before an external change can replace local work.

Text follows the desktop text size — `linux display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
Writer at the size it is designed around; larger and smaller sizes scale from there.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`
- `xdg-desktop-portal` and a portal backend

