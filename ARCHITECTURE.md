# Skred WebKit Application Architecture

## Purpose

ro-totem is the reference implementation for building portable Skred audio
applications with:

- a C executable as the native host;
- Skred as the audio and command engine;
- the operating system's WebKit implementation as the user interface;
- HTML, CSS, and JavaScript stored as a local application resource; and
- a small text protocol connecting JavaScript to C.

This structure is intended for audio tools that need a custom graphical
interface without adopting a large cross-platform GUI framework. The native
host remains small, while the UI uses familiar browser technologies.

The current implementation deliberately keeps the application-specific C code
in `rototem.c` and the complete UI in `ui.html`. Future projects may retain that
layout when their scope is similar. Organization inside each file matters more
than splitting code merely to reduce line counts.

## Architectural Overview

```text
┌──────────────────────────────────────────────────────────┐
│ ui.html                                                  │
│                                                          │
│ HTML/CSS      Custom elements      Application state     │
│      └──────────────┬────────────────────┘                │
│                     │                                    │
│              JavaScript actions                          │
│                     │                                    │
│              native bridge object                        │
└─────────────────────┼────────────────────────────────────┘
                      │ window.external.invoke(message)
                      ▼
┌──────────────────────────────────────────────────────────┐
│ rototem.c                                                │
│                                                          │
│ invoker() protocol dispatcher                            │
│   ├── engine commands ───────────────────────┐            │
│   ├── file and directory dialogs             │            │
│   ├── settings persistence                   │            │
│   ├── audio-device management                │            │
│   └── wave loading                           │            │
│                                              ▼            │
│                                      Skred engine         │
└──────────────────────────────────────────────────────────┘
                      │
                      │ webview_eval(script)
                      ▼
┌──────────────────────────────────────────────────────────┐
│ Named JavaScript callback updates UI state and rendering │
└──────────────────────────────────────────────────────────┘
```

The UI-to-native path sends a string through `window.external.invoke()`. The
native-to-UI path evaluates a call to a named JavaScript function. Neither side
accesses the other's internal state directly.

## Responsibility Boundaries

### Skred

Skred owns:

- real-time audio processing;
- voices, wavetables, playback, levels, and other engine state;
- the compact audio command language;
- audio-device discovery and connection behavior; and
- engine log and status messages.

Skred should not know about buttons, DOM elements, dialogs, or settings-file
presentation.

### Native C Host

The C host owns capabilities that require native APIs or process-level control:

- application startup and shutdown;
- creating and running the webview;
- locating bundled resources;
- opening native file and directory dialogs;
- reading and writing settings files;
- loading file-backed waveforms and allocating their wavetable numbers;
- enumerating and applying audio devices;
- validating messages received from JavaScript;
- translating native results into JavaScript callbacks; and
- starting and stopping Skred.

The host is an adapter between the operating system, WebKit, and Skred. It
should contain little presentation policy.

### HTML and CSS

HTML and CSS own:

- semantic controls and status regions;
- layout, color, typography, and responsive behavior;
- accessibility labels and control state; and
- reusable visual components such as `control-slider` and
  `voice-pair-card`.

Application behavior should not be embedded in inline event attributes. The
document uses `data-action` and delegated listeners so generated and static
controls follow the same event path.

### JavaScript

JavaScript owns:

- application-facing state;
- rendering and updating controls;
- mapping user actions to native bridge methods;
- requesting waveform loads without constructing file-backed Skred commands;
- settings serialization and migration;
- batching related engine commands; and
- receiving named callbacks from C.

JavaScript state should describe what the UI believes is active. Skred remains
the authority for engine behavior. For operations that can fail, update
persistent UI state after receiving the native result rather than immediately
after sending the request.

## The Two Command Layers

There are two related but separate protocols.

### UI-to-Native Envelope

The first character, or short prefix, tells `invoker()` which native operation
to perform:

| Message | Meaning |
| --- | --- |
| `!<commands>` | Pass commands to Skred |
| `@` | Open the wave-directory chooser |
| `>v<voice>` | Open a wave chooser for a stereo voice pair |
| `R<directory>` | Scan a directory for WAV files |
| `W<voice>:<path>` | Load a WAV file into a stereo voice pair |
| `JS<json>` | Save settings JSON |
| `JL` | Load settings JSON |
| `PB` | Begin saving a project ZIP |
| `PW<index>:<archive-path>:<source-path>` | Add a WAV to the pending project ZIP |
| `PF<json>` | Add settings and finish the pending project ZIP |
| `PL` | Load a project ZIP |
| `PA` / `PR` | Accept or reject an extracted project after UI validation |
| `DR` | Refresh audio-device choices |
| `DA<capture>:<selection>` | Apply an audio-device selection |

The JavaScript `native` object is the public API for constructing these
messages. UI code should call `native.loadWave(...)` or
`native.saveSettings(...)`, not manually concatenate protocol prefixes
throughout the application.

On the C side, `enum native_command` and `invoker()` define the corresponding
entry points. Additions should be made on both sides together.

### Skred Engine Commands

The payload following `!` belongs to Skred. Common examples used by ro-totem
include:

| Command form | Meaning |
| --- | --- |
| `v<n>a<x>` | Set voice volume |
| `v<n>n<x>` | Set voice speed |
| `v<n>l<0\|1>` | Stop or start a voice |
| `v<n>m<0\|1>` | Unmute or mute a voice |
| `v<n>b0` | Set forward playback |
| `v<n>b1` | Set backward playback |
| `v<n>b-` | Invert the current playback direction |

The `b` command accepts a numeric parameter. A bare `-` is parsed as `NaN`,
which Skred intentionally interprets as "invert the current direction." Use
`b-` for a flip rather than a bare `b`: bare `b` works at the end of an
isolated command line, but its parameter parsing can consume part of an
immediately following command.

These forms are domain commands, not native bridge operations. Keeping that
distinction explicit prevents UI concerns from leaking into Skred and prevents
engine syntax from becoming the C dispatcher's responsibility.

When a future project introduces many application-level operations or payloads
that can contain arbitrary delimiters, consider replacing the envelope with
JSON messages such as:

```json
{
  "type": "loadWave",
  "voice": 4,
  "path": "/path/to/file.wav"
}
```

The compact protocol is appropriate while it remains small, local, and easy to
validate. JSON becomes worthwhile when escaping and versioning would otherwise
dominate the dispatcher.

## Native-to-UI Calls

C calls named JavaScript functions with `webview_eval()`. Examples include:

- `addLog(message)`
- `setWaveDirectory(directory)`
- `clearWaveFiles()`
- `addWaveFile(filename)`
- `setTrackWave(...)`
- `waveFileLoaded()`
- `loadSettingsFromText(json)`
- `audioDevicesReady(status)`
- `audioDeviceApplied(kind, success, status)`

All dynamic strings must pass through `script_append_js_string()`. This is both
a correctness and safety requirement: paths, device names, logs, and JSON may
contain quotes, slashes, control characters, or other text that would otherwise
break the generated JavaScript.

The `script_builder` abstraction provides dynamically sized, bounded script
construction. New native callbacks should use it instead of fixed buffers or
raw `sprintf()` calls.

File-backed waveform loading is intentionally centralized in C. JavaScript may
choose a path and call `native.loadWave(voice, path)`, but C allocates wavetable
numbers and constructs the `[file] /ws` and `wt` commands. Picker, random, and
settings-restoration paths therefore share one implementation and one allocator.

A useful callback convention is:

```text
JavaScript action
    -> native.requestOperation(...)
    -> C validates and performs operation
    -> C calls operationCompleted(success, status, data...)
    -> JavaScript updates state and presentation
```

This convention becomes especially important for device changes, file I/O, and
other operations that may fail.

## UI State and Rendering

ro-totem defines static track configuration in `TRACKS` and mutable state in
`trackState`.

```javascript
const trackState = TRACKS.map(({ track }) => ({
    visible: track < 4,
    locked: false,
    playing: false,
    muted: false,
    wave: null
}));
```

Each track's related state is kept together. This is preferable to parallel
maps because adding a property requires changing one model rather than keeping
several collections synchronized.

Use the following pattern when adding a feature:

1. Add the durable value to the appropriate state object.
2. Add a small setter that validates and stores the value.
3. Add a render/update function for affected DOM elements.
4. Have event handlers call the setter or a named action function.
5. Include the value in settings only if it should survive application restarts.

Avoid treating DOM text or CSS classes as the primary source of application
state. The DOM is a rendering of state, not the state model itself.

## Custom Elements and Event Delegation

Custom elements are useful for controls that have a repeated internal
structure and behavior. `control-slider` encapsulates its range input, display,
styling, and formatted update event. `voice-pair-card` generates one repeated
track interface.

Use a custom element when:

- a visual control is repeated;
- its internal markup should be treated as one component;
- it has a clear attribute-based input contract; or
- it emits a small, reusable event contract.

Do not turn every container into a custom element. Ordinary rendering
functions are simpler for application-specific groups.

The document-level event handlers use `data-action` and `data-setting`:

```html
<button data-action="set-track-playing"
        data-track="2"
        data-playing="true">go</button>
```

This lets dynamically generated controls work without individual listener
setup. To add an action:

1. Give the control a descriptive `data-action`.
2. Store simple parameters in `data-*` attributes.
3. Add one case to the delegated action dispatcher.
4. Call a named function that contains the actual behavior.

The dispatcher should remain routing code. Complex behavior belongs in named
functions that can be read and tested independently.

## Settings Design

Settings files have a format identifier and integer version:

```json
{
  "format": "ro-totem-slider-settings",
  "version": 9,
  "tracks": []
}
```

This permits forward development without making old user files unreadable.
When changing settings:

1. Increment the version for a schema or behavioral change.
2. Continue accepting versions that can be migrated safely.
3. Give older versions explicit defaults.
4. Validate types, ranges, and array entries before applying them.
5. Apply engine commands after dependent waves and devices are ready.

ro-totem uses pending restore state because wave loading is asynchronous from
the UI's perspective. The saved slider and mute commands are applied only after
all requested wave loads report completion.

The configurable Controls window is stored in the same settings payload under
`commandSliders`. Each entry preserves its command template, minimum, maximum,
step, and current value. Four sliders are created by default; users can keep
between four and eight, and the array length restores that count. Those
commands are restored after any project WAV files finish loading, before saved
track and master controls so the dedicated controls remain authoritative when
both target the same engine parameter.

The project also stores the main content size plus the dimensions and open
state of the REPL and Controls windows under `uiWindows`. Window positions are
intentionally not portable project state. The main size is restored through
the native bridge; floating-panel dimensions preserve their current positions
where possible, are clamped to the current viewport, and then their saved
visibility is restored.

Keep settings declarative. Save values such as ranges, device identity, wave
paths, and mute state rather than replaying an opaque history of UI actions.

Project ZIP files contain `settings.json` and WAV entries under `waves/`.
Original WAV basenames are preserved. If distinct source paths share a
basename, later entries receive a suffix such as ` (2)`; characters that are
not portable as Windows filenames are replaced with `_`. The archived settings
use those relative paths. Native loading validates the complete ZIP, rejects
unexpected names and duplicate entries, applies size and count limits, and
extracts into an app-owned temporary directory. JavaScript resolves the
relative paths before calling the normal settings restoration flow. Temporary
project files are removed when a different project is loaded or the
application exits.

## Application Lifecycle

The native lifecycle is:

1. Locate `ui.html` relative to the executable or application bundle.
2. Convert the path into a valid `file://` URL.
3. Enumerate audio devices.
4. Configure and start Skred.
5. Initialize the webview and register `invoker()`.
6. Run the webview event loop.
7. Send Skred its quit command when the window closes.

The UI lifecycle is:

1. Define custom elements and constants.
2. Create configuration and mutable state.
3. Render track cards and advanced settings.
4. Register delegated events.
5. Request the initial audio-device list.
6. Receive native callbacks as operations complete.

Do not perform real-time audio work in JavaScript or in UI callbacks. WebKit
controls and the webview event loop are not real-time facilities. Skred should
own audio processing and any synchronization required by its audio thread.

## Portability

The architecture is portable because the application uses a small webview
wrapper over native browser components:

- macOS uses Cocoa/WebKit with `WEBVIEW_COCOA`;
- Linux uses GTK and WebKitGTK with `WEBVIEW_GTK`;
- the included webview implementation also has a Win32 backend for projects
  that add and maintain the corresponding build and packaging path.

The UI should avoid assumptions tied to a full desktop browser:

- load local resources through paths supplied by the native host;
- do not depend on a development server;
- do not assume browser extensions or Node.js APIs;
- test CSS controls in each target WebKit implementation;
- use native dialogs for filesystem access; and
- treat refresh as application reinitialization.

Platform-specific code should remain in small `#ifdef` sections around resource
lookup, platform defaults, and packaging behavior. Feature logic should remain
platform-neutral.

## Build and Packaging Pattern

For a new project, the minimum runtime package is:

```text
application executable
ui.html
Skred library and required runtime assets
platform metadata and icon
```

On Linux, compile the host against GTK, WebKitGTK, and the Skred library. The
current `linux-bundle` target demonstrates the required flags.

On macOS, place the executable under `Contents/MacOS`, `ui.html` under
`Contents/Resources`, and metadata under `Contents/Info.plist`. Sign the final
bundle after copying all resources.

Keep resource lookup independent of the process's working directory. Users may
launch an application from a desktop shell, file manager, or another process,
so the current directory is not a reliable resource location.

## Starting a New Skred WebKit Project

Use ro-totem as a template in the following order.

### 1. Define the Audio Model

Decide:

- how many voices the application needs;
- which voices form logical tracks or stereo pairs;
- which Skred commands the UI must expose;
- which state must be restored; and
- which operations can fail or complete later.

### 2. Keep the Native Host Small

Retain the reusable host facilities:

- resource-path and file-URL handling;
- `script_builder`;
- JavaScript string escaping;
- log forwarding;
- settings file I/O;
- webview setup and lifecycle; and
- the validated `invoker()` dispatch pattern.

Replace project-specific wave, track, and device actions as needed.

### 3. Define the Bridge Before Building Controls

Write the JavaScript `native` object and matching C command cases first. Give
each operation a named method. Document payload formats beside the bridge.

For every operation, answer:

- What does JavaScript send?
- What does C validate?
- Which subsystem performs the work?
- How does success or failure return to JavaScript?

### 4. Define Configuration and State

Separate immutable configuration from mutable state. For example:

```javascript
const CHANNELS = [
    { channel: 0, voice: 0, color: '#ff5722' }
];

const channelState = CHANNELS.map(() => ({
    active: false,
    source: null
}));
```

### 5. Build Repeated UI Components

Use custom elements or rendering functions for repeated channels, tracks,
meters, envelopes, or effect controls. Emit descriptive events rather than
calling the native bridge from deep inside every component.

### 6. Add Versioned Settings

Choose a project-specific format string from the beginning. Include only
durable state, validate all loaded data, and provide defaults for previous
versions.

### 7. Verify on Every Target Platform

At minimum, verify:

- the native executable compiles and links;
- the JavaScript parses in the target WebKit engine;
- paths with spaces, quotes, and non-ASCII characters work;
- file dialogs can be canceled safely;
- settings round-trip correctly;
- missing files and audio devices produce visible errors;
- repeated refreshes do not duplicate state or listeners; and
- closing the window shuts down Skred cleanly.

## Extension Rules

These rules keep projects built from this model understandable:

1. UI code calls named methods on `native`; it does not scatter raw bridge
   prefixes.
2. C validates every value received across the bridge.
3. Dynamic C-to-JavaScript strings use `script_append_js_string()`.
4. Skred commands remain distinct from UI-to-native messages.
5. File-backed waveform loading and wavetable allocation remain in C.
6. Repeated mutable data lives in one state object per logical unit.
7. DOM updates are performed by named rendering functions.
8. Operations that can fail return a named completion callback.
9. Settings are versioned and validated before application.
10. Audio processing remains outside the WebKit/UI thread.
11. Platform-specific behavior stays narrow and explicit.

## When to Outgrow the Single-File Layout

Keeping one C file and one HTML file is reasonable while:

- one developer can navigate each file comfortably;
- the bridge remains small;
- components share one application state model;
- builds do not require generated JavaScript bundles; and
- changes can be reviewed without unrelated sections moving.

Consider splitting files only when there is a concrete ownership or tooling
benefit, such as reusable host code shared by several applications, generated
UI assets, independent component testing, or multiple developers repeatedly
editing the same regions.

Even if future projects split files, preserve the same architectural
boundaries. File count is an implementation choice; the separation between
Skred, native capabilities, bridge messages, application state, and rendering
is the reusable design.

## Reference Files

- `rototem.c`: native host, bridge dispatcher, Skred integration, persistence,
  and application lifecycle.
- `ui.html`: complete local UI, native bridge API, state, rendering, actions,
  and settings migration.
- `vendor/skred/`: externally produced Skred API header and platform static
  libraries.
- `vendor/webview/`: vendored native webview abstraction and platform
  backends.
- `Makefile`: Linux, macOS, and preliminary Zig-based Windows build and
  packaging targets.
- `assets/Info.plist`: macOS bundle metadata.

ro-totem should remain a working example first and a framework second. New
abstractions belong here when they have already proved useful across multiple
applications, not merely because they might someday be reusable.
