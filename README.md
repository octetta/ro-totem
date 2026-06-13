# ro-totem

[![CodeFactor](https://www.codefactor.io/repository/github/octetta/ro-totem/badge)](https://www.codefactor.io/repository/github/octetta/ro-totem)

ro-totem is a portable Skred audio application and the reference project for
building native audio tools with a C host and a local WebKit user interface.

The application keeps the audio engine and operating-system integration in C,
while HTML, CSS, and JavaScript provide the interface. A small documented
message protocol connects the two layers.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for:

- responsibility boundaries between Skred, C, and JavaScript;
- the UI-to-native and Skred command protocols;
- waveform loading, settings, state, and callback patterns;
- portability and packaging guidance; and
- a checklist for starting another Skred WebKit application.

## Source Layout

- `rototem.c`: native host, Skred integration, persistence, and bridge
  dispatcher.
- `ui.html`: complete WebKit interface, state model, controls, and actions.
- `vendor/skred/`: externally produced Skred API header and platform static
  libraries.
- `vendor/miniz/`: miniz 3.1.1 amalgamated ZIP implementation and license.
- `vendor/webview/`: vendored portable webview implementation and platform
  backends.
- `Makefile`: host-aware Linux, macOS, and preliminary Windows build targets.

## Projects and Settings

The main `load zip` and `save zip` actions use a portable project archive
containing `settings.json`, the WAV files used by the current tracks, and files
added through the Files window. WAV basenames are preserved, with numbered
suffixes only when names collide. Managed files can be referenced from the
REPL or Commands window as `{{file:name}}`; references are resolved when a
command is explicitly sent. Standalone JSON settings import and export remain
available under `advanced`; those files continue to reference source files by
their original filesystem paths.

The Commands window is available with `Ctrl+Shift+1` and numbers entries from
zero. Entry `0` is available for configuration; the Pads window uses
`Ctrl+Shift+2` and maps Pads `1` through `8` to Commands `1` through `8`. The Files window uses
`Ctrl+Shift+3`. Project JSON and ZIP files preserve each floating window's
visibility, size, and position within the main window. Each Pads layout keeps
its own saved size and position. Floating-window order and the last focused
window are restored as well.

`Ctrl+Alt+H` temporarily hides or restores all floating windows on Linux and
Windows. On macOS, use `Cmd+Option+H`. The restore keeps the previous visible
set, stacking order, geometry, and focus.

Advanced settings include an opt-in portable window geometry mode. It preserves
the current platform's main-window size and restores floating windows
proportionally, while the default mode continues using absolute saved geometry.

The REPL keeps submitted commands in a history available with the Up and Down
arrow keys. History is preserved in JSON and project ZIP files. Submitted text
is cleared from the entry field, and the log colors entered commands separately
from Skred output and errors.

## Linux Build

Install GTK 3 and WebKitGTK development packages. On Fedora:

```sh
sudo dnf install gtk3-devel webkit2gtk4.0-devel
make
```

On Linux, the default target creates a portable archive under `dist/`. The
archive keeps the `rototem` executable, `ui.html`, icon, and launch
instructions together. Extract it and run `./rototem`.

Use `make linux` to compile only the executable under `build/linux/`.

For AppImage packaging:

```sh
mise install
make appdir
make appimage
```

These targets create `dist/ro-totem.AppDir/` and an executable AppImage under
`dist/`. The AppImage currently relies on the host GTK 3 and WebKitGTK runtime
libraries.

## macOS Build

With Xcode command-line tools installed:

```sh
make
```

The default target builds, signs, and archives `ro-totem.app`. The application
bundle includes `ui.html` and its icon under `Contents/Resources`.

The Makefile detects Linux, macOS, and Windows automatically. Explicit
platform package targets are also available.

## Windows Build

The preliminary Windows target uses Zig's C compiler with the
`x86_64-windows-gnu` target:

```sh
make windows-check
make windows-package
```

`windows-check` lists the Windows Skred and WebView2 files that still need to
be supplied. The resulting package will keep `ro-totem.exe` and `ui.html`
together under `dist/`.
