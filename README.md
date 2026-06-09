# ro-totem

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
- `api.h`: Skred API used by the host.
- `webview.h`: portable webview implementation.
- `Makefile`: Linux build and macOS bundle targets.

## Linux Build

Install GTK 3 and WebKitGTK development packages. On Fedora:

```sh
sudo dnf install gtk3-devel webkit2gtk4.0-devel
make linux-bundle
```

This creates the `rototem` executable in the repository root.

## macOS Build

With Xcode command-line tools installed:

```sh
make
```

The default target builds, signs, and archives `ro-totem.app`. The application
bundle includes `ui.html` and its icon under `Contents/Resources`.
