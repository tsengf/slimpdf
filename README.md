# SlimPDF

A lightweight, cross-platform PDF reader built with C++ and Qt6.

## Features

- **Non-locking file access** — the file handle is released immediately after rendering, so you can rename, move, or replace the PDF while it is open
- **Auto-refresh** — watches the file for changes and reloads automatically, preserving your scroll position
- **Zoom controls** — zoom in/out and 1:1 reset via toolbar icons
- **Fit modes** — fit to width, height, or full page; the active mode is highlighted in the toolbar and reapplied on window resize
- **Multi-page scrolling** — all pages are rendered in a single continuous scroll view
- **Page indicator** — status bar shows "Page X of N"; spanning two pages shows "Pages X–Y of N"
- **Text selection** — drag to select text; the highlight follows individual text lines (word-processor style) and the selected text is copied to the clipboard
- **Recent files** — persisted across sessions; accessible from the macOS Dock icon right-click menu under **Open Recent**
- **Drag-and-drop** — drop a PDF onto the window to open it

## Requirements

| Dependency | Version |
|------------|---------|
| Qt         | 6.4 or later (Core, Gui, Widgets, Pdf, Svg) |
| CMake      | 3.19 or later |
| C++        | 17 |

On macOS, Objective-C++ support is also required (included with Xcode).

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting app bundle is at `build/slimpdf.app` (macOS) or `build/slimpdf` (Linux/Windows).

### macOS (MacPorts)

```bash
sudo port install qt6 cmake
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/local/libexec/qt6
cmake --build build --parallel
```

### Linux (apt)

```bash
sudo apt install cmake qt6-base-dev qt6-pdf-dev libqt6svg6-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Usage

```bash
# Open the file picker
./build/slimpdf.app/Contents/MacOS/slimpdf

# Open a specific file
./build/slimpdf.app/Contents/MacOS/slimpdf document.pdf
```

**Keyboard shortcuts**

| Action | Shortcut |
|--------|----------|
| Open file | Ctrl+O |
| Zoom in | Ctrl++ |
| Zoom out | Ctrl+- |
| Actual size (1:1) | Ctrl+0 |

## Project structure

```
SlimPDF/
├── CMakeLists.txt
├── icons/              # SVG toolbar icons
│   ├── zoom-in.svg
│   ├── zoom-out.svg
│   ├── zoom-reset.svg
│   ├── fit-width.svg
│   ├── fit-height.svg
│   └── fit-page.svg
└── src/
    ├── main.cpp
    ├── main_window.h
    ├── main_window.cpp
    ├── macos_dock.h
    └── macos_dock.mm   # macOS Dock right-click menu (Objective-C++)
```

## License

MIT
