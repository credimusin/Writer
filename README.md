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

If you use Arch Linux, you can build and install the package using the provided `PKGBUILD`:
```sh
cd pkgbuild
makepkg -si
```

For manual installation, copy the executable and desktop file:
```sh
sudo install -Dm755 build/writer /usr/bin/writer
sudo install -Dm644 pkgbuild/writer.desktop /usr/share/applications/writer.desktop
sudo install -Dm644 pkgbuild/writer.svg /usr/share/icons/hicolor/scalable/apps/writer.svg
```

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Writer window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Unsaved drafts are recovered after an abnormal exit. Writer also watches open files
and warns before an external change can replace local work.

Text follows the desktop text size — `linux display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
Writer at the size it is designed around; larger and smaller sizes scale from there.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`
- `xdg-desktop-portal` and a portal backend


