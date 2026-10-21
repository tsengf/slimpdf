# SlimPDF

SlimPDF is a small desktop PDF reader built with C++17 and Qt 6. It combines continuous page scrolling, live text selection, and automatic reloads so you can keep reading while a PDF changes on disk.

## Features

- **Continuous reading.** Scroll through all pages in one window, with the page number and zoom level in the status bar.
- **Page-aware status.** When at least 90% of a page is visible, the status bar shows that page alone, such as `Page 3 of 10`. Otherwise, it shows the visible range, such as `Pages 3–4 of 10`. If only one page is visible, it shows a single page number.
- **Live text selection.** The pointer becomes an I-beam over selectable text. Click and drag in either direction to highlight text across lines within a page. Highlights follow the text as you drag, and stay visible when you release the mouse. Use **Edit → Copy** or the copy shortcut to copy the selected text.
- **Zoom and fit controls.** Fit Width is active by default. Zoom from 25% to 400%, reset to 100%, or fit the document to the window's width, height, or full page. The active fit button stays highlighted and the fit is recalculated when you resize the window. Manual zoom turns off the fit mode.
- **Automatic reloads.** Changes to the open PDF trigger a reload after a short delay, preserving the vertical scroll position.
- **Files stay available.** SlimPDF reads the PDF into memory and closes the source file before rendering. You can rename, move, or replace the file while its current contents remain visible. Automatic reloads watch the original path; reopen the PDF if you move or rename it.
- **Recent files.** Recently opened files persist across sessions under **File → Open Recent**, with an option to clear the list. On macOS, the list follows the system's recent-document limit, including zero, and is also available from the Dock icon's **Open Recent** menu. Linux and Windows retain up to ten files.
- **Flexible opening.** Open a PDF from the File menu, drop it onto the window, or pass its path on the command line.

Text selection uses the PDF's existing text layer. Image-only scans need a text layer before their text can be selected.

## Using SlimPDF

Launching SlimPDF without a file opens an empty reader window. Choose **File → Open…**, drag a PDF onto the window, or choose a recent file to start reading. Opening another PDF replaces the document in that window.

Use the toolbar or **View** menu for zoom and fit controls. Click an active fit button again to turn off automatic fitting while keeping the current zoom.

Choose **Help → About SlimPDF…** to see the version and repository link. On macOS, **About SlimPDF…** appears in the application menu.

### Keyboard and mouse controls

| Action | macOS | Linux / Windows |
| --- | --- | --- |
| Open a file | ⌘O | Ctrl+O |
| Zoom in | ⌘+ | Ctrl++ |
| Zoom out | ⌘− | Ctrl+− |
| Reset zoom to 100% | ⌘0 | Ctrl+0 |
| Zoom with the mouse wheel | ⌘ + scroll | Ctrl + scroll |
| Select text | Drag over text, then release | Drag over text, then release |
| Copy selected text | ⌘C | Ctrl+C |

### Launch from the command line

On macOS:

```bash
./build/slimpdf.app/Contents/MacOS/slimpdf
./build/slimpdf.app/Contents/MacOS/slimpdf document.pdf
```

On Linux:

```bash
./build/slimpdf document.pdf
```

On Windows, run `slimpdf.exe` from the build output directory. With a multi-configuration generator such as Visual Studio, it is typically under `build/Release/`.

## Installation packages

Native packages are built by the **Installation packages** GitHub Actions workflow. Once the workflow has run, download its artifacts from the repository's [Actions page](https://github.com/tsengf/slimpdf/actions/workflows/packages.yml).

| Platform | Package | Installation |
| --- | --- | --- |
| macOS | `.dmg` | Open the disk image and drag `slimpdf.app` to Applications. CI builds a universal app for Apple silicon and Intel Macs. |
| Windows x64 | `.exe` | Run the installer. It adds a Start menu shortcut and an uninstaller. |
| Ubuntu 24.04 x64 | `.deb` | Run `sudo apt install ./slimpdf_*.deb`. |
| Fedora 43 x64 | `.rpm` | Run `sudo dnf install ./slimpdf-*.rpm`. |

macOS and Windows packages include the Qt runtime. Linux packages use the distribution's Qt packages; the package manager installs the required dependencies. Linux packages should be built on the distribution and release they target.

macOS packages are ad-hoc signed, without Apple notarization. Windows installers are unsigned. Public distribution with a verified publisher requires the appropriate signing certificates.

### Build packages locally

Build on the platform you want to package for. macOS and Windows packaging require Qt 6.7 or later and its deployment tools. Windows also requires NSIS 3.03 or later on `PATH`. Linux `.deb` builds require `dpkg-dev`; `.rpm` builds require `rpm-build`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSLIMPDF_BUILD_PACKAGES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release
```

Add `-DCMAKE_PREFIX_PATH=/path/to/qt` if needed. On Linux, choose the native package format by adding `-G DEB` or `-G RPM` to the `cpack` command. Generated packages are written to `build/packages/`, with the version and architecture in their names.

On macOS, the package contains the architecture built locally. With a universal Qt installation, add `'-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'` when configuring to include both architectures.

The workflow runs on pushes to `main`, version tags, pull requests, and manual dispatch. It builds and tests each platform, then uploads packages as workflow artifacts for 30 days. It does not publish GitHub Releases automatically.

## Build from source

Requirements:

- A C++17 compiler.
- CMake 3.19 or later.
- Qt 6.4 or later with Core, Gui, Widgets, Pdf, and Svg.
- Qt Test for the test suite, which is enabled by default.
- On macOS, an Objective-C++ compiler and the AppKit SDK, provided by Xcode's developer tools.

From the project directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

If CMake cannot find Qt, pass its installation prefix with `-DCMAKE_PREFIX_PATH=/path/to/qt`.

For a macOS Qt installation managed by MacPorts:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/local/libexec/qt6
cmake --build build --config Release --parallel
```

The application is built as `build/slimpdf.app` on macOS, `build/slimpdf` on Linux, or `slimpdf.exe` in the Windows build output directory. Multi-configuration generators may add a configuration directory such as `Release`.

To build the application without the test suite or Qt Test dependency, add `-DBUILD_TESTING=OFF` to the CMake configuration command.

## Tests

After building, run:

```bash
ctest --test-dir build -C Release --output-on-failure
```

The Qt Test suite renders widgets into images. It runs offscreen on macOS and Linux and uses the native Windows platform for access to installed fonts. It checks text cursor behavior, live highlights, forward and backward selection across lines, horizontal selection, and clearing selections at three zoom levels. It also checks that selection leaves the clipboard unchanged and that the Copy command copies only the current selection.

## Source layout

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Application startup and command-line file opening |
| `src/main_window.h`, `src/main_window.cpp` | Reader window, page rendering, selection, zoom, file watching, and recent files |
| `src/macos_dock.h`, `src/macos_dock.mm` | macOS Dock recent-file menu |
| `icons/` | SVG toolbar icons embedded in the application |
| `tests/text_selection_test.cpp` | Text selection regression tests |
| `CMakeLists.txt` | Application and test build configuration |
| `cmake/Packaging.cmake` | Native installer configuration and Qt deployment |
| `packaging/linux/slimpdf.desktop` | Linux application menu entry and PDF MIME type |
| `.github/workflows/packages.yml` | Native package builds for macOS, Windows, Ubuntu, and Fedora |

## License

[MIT](LICENSE)
