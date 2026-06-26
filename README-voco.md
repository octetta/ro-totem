# Voco

Voco is a small framed-message bridge for C applications that use a browser UI
through a string-only webview interface. It was extracted from ro-totem, which
uses the 2017-style `webview.h` API:

- JavaScript sends native messages with `window.external.invoke(frame)`.
- C sends UI messages by evaluating `Voco.receive(frame)`.
- Both directions use the same frame format.

The goal is not to replace application protocols or domain languages. Voco is a
transport layer for carrying named commands and payload fields safely across a
raw string wire.

## Philosophy

Voco is built around a few constraints common to small `webview.h` apps:

- The bridge only moves strings.
- The native side should not need a JSON parser for routine dispatch.
- File paths, JSON blobs, logs, device names, and non-ASCII text must survive
  the bridge without delimiter bugs.
- The UI should call named app methods, not scatter raw transport strings.
- The C side should parse frames into borrowed slices and allocate only when an
  application actually asks for a decoded copy.

The design is intentionally modest. A Voco frame is easy to print, inspect,
construct, and reject. It does not provide promises, object schemas, binary
buffers, threading, retries, or security boundaries. Those belong in the host
application.

## Files

The reusable pieces are:

```text
voco.h   # Header-only C parser, field decoder, and frame writer helpers
voco.js  # Browser-side frame formatter, parser, and namespace dispatcher
```

ro-totem keeps app-specific command dispatch in `rototem.c` and app-specific UI
handlers in `ui.html`.

## Wire Format

A frame starts with the magic string `V1`, followed by length-prefixed fields:

```text
V1<length>:<field><length>:<field>...
```

The first three decoded fields are conventionally:

| Field | Meaning |
| --- | --- |
| `namespace` | Message domain such as `native`, `ui`, or `scope` |
| `command` | Named operation inside that namespace |
| `type` | A light payload hint such as `A`, `N`, `J`, or `F` |

Any remaining fields are command-specific payload values.

Example decoded message:

```text
native, loadWave, A, 4, /Users/me/Samples/kick:01.wav
```

Encoded frame:

```text
V16:native8:loadWave1:A1:432:%2FUsers%2Fme%2FSamples%2Fkick%3A01.wav
```

Fields are percent-encoded before length-prefixing. That keeps the wire ASCII
and makes lengths mean the same thing to C byte strings and JavaScript string
indexes. The length counts the encoded field text, not the decoded text.

## Type Hints

Voco does not enforce payload types. The `type` field is a convention for the
application dispatcher.

ro-totem currently uses:

| Hint | Meaning |
| --- | --- |
| `A` | Text payloads |
| `N` | Numeric payloads represented as text |
| `J` | JSON text payload |

Future applications may add hints such as `F` for base64-encoded float buffers.
The parser treats all fields as strings.

## JavaScript API

`voco.js` installs `window.Voco`.

### `Voco.format(...fields)`

Formats fields into a Voco frame.

```javascript
const frame = window.Voco.format(
    'native', 'loadWave', 'A', 4, '/tmp/kick:01.wav');
window.external.invoke(frame);
```

Each argument is converted with `String(field ?? '')`, percent-encoded with
`encodeURIComponent`, and length-prefixed.

### `Voco.parse(frame)`

Parses a frame and returns decoded fields, or `null` if the frame is malformed.

```javascript
const fields = window.Voco.parse(frame);
if (!fields) return;
```

### `Voco.on(namespace, handler)`

Registers a namespace handler.

```javascript
window.Voco.on('ui', (command, type, payload) => {
    if (command === 'setStatus') {
        setStatus(payload[0] || '');
    }
});
```

Only one handler is stored per namespace. Applications that need multiple
listeners can implement their own fan-out inside the handler.

### `Voco.receive(frame)`

Parses a frame and dispatches it to the namespace handler.

```javascript
window.Voco.receive('V12:ui9:setStatus1:A5:ready');
```

Returns `true` when a registered handler receives the message and `false` when
the frame is malformed or the namespace is unknown.

## C API

`voco.h` is header-only. It provides borrowed parsing and optional decoded
copies.

### Data Structures

```c
struct voco_field {
  const char *data;
  size_t length;
};

struct voco_message {
  int count;
  struct voco_field fields[VOCO_MAX_FIELDS];
};
```

`voco_parse()` stores field slices that point into the original frame string.
Those slices remain valid only while the original string remains valid.

### Parsing

```c
struct voco_message message;
if (!voco_parse(arg, &message)) {
  return;
}
```

`voco_parse()` validates the `V1` magic, parses each length prefix, bounds-checks
field lengths, and fills `message.fields`. It does not percent-decode fields
in place.

### Comparing Fields

```c
if (voco_field_equals(&message.fields[0], "native")) {
  /* route native message */
}
```

Use this for namespace and command dispatch when comparing against ASCII command
names. Command names should stay in the unreserved ASCII set.

### Decoding Text

```c
char *path = voco_field_cstr(&message.fields[3]);
if (!path) return;
load_wave(path);
free(path);
```

`voco_field_cstr()` percent-decodes a field into a heap-allocated,
NUL-terminated C string. The caller owns the returned pointer and must `free()`
it.

### Parsing Numbers

```c
long voice;
if (!voco_field_long(&message.fields[3], 0, 31, &voice)) {
  return;
}
```

`voco_field_long()` parses a borrowed field as a bounded integer. It expects the
field to contain plain numeric text.

### Writing Frames

Voco does not own a growable string type. Instead, it writes through an append
callback:

```c
typedef int (*voco_append_fn)(
    void *context, const char *text, size_t length);
```

The host application provides the buffer policy. In ro-totem, the callback
adapts Voco to `script_builder`:

```c
static int append_to_script(
    void *context, const char *text, size_t length) {
  return script_append_n((struct script_builder *)context, text, length);
}
```

Build a frame header:

```c
struct script_builder frame = {0};
if (!voco_write_frame_header(
      &frame, append_to_script, "ui", "setStatus", "A")) {
  /* handle allocation failure */
}
```

Append payload fields:

```c
voco_write_text_field(&frame, append_to_script, "ready");
voco_write_int_field(&frame, append_to_script, 42);
voco_write_u64_field(&frame, append_to_script, first_frame);
```

`voco_write_text_field()` percent-encodes text before writing its length prefix.
`voco_write_int_field()` and `voco_write_u64_field()` write numeric text fields
without percent-encoding because their output is already ASCII.

## Webview Integration Pattern

### JavaScript to C

Define an app-specific bridge object in JavaScript. It should be the only code
that calls `window.external.invoke()`.

```javascript
const native = {
    available() {
        return Boolean(window.external && window.external.invoke);
    },
    invoke(command, type = 'A', ...payload) {
        if (!this.available()) return false;
        window.external.invoke(
            window.Voco.format('native', command, type, ...payload)
        );
        return true;
    },
    loadWave(voice, path) {
        return this.invoke('loadWave', 'A', voice, path);
    }
};
```

In C, parse the incoming string and dispatch by namespace and command:

```c
static void invoker(struct webview *w, const char *arg) {
  struct voco_message message;
  if (!voco_parse(arg, &message)) return;

  if (!voco_field_equals(&message.fields[0], "native")) return;
  if (voco_field_equals(&message.fields[1], "loadWave")) {
    long voice;
    char *path;
    if (!voco_field_long(&message.fields[3], 0, 31, &voice)) return;
    path = voco_field_cstr(&message.fields[4]);
    if (!path) return;
    load_wave_file(w, path, (int)voice);
    free(path);
  }
}
```

### C to JavaScript

Build a Voco frame in C and evaluate one JavaScript receiver call:

```c
static void send_status(struct webview *w, const char *message) {
  struct script_builder frame = {0};
  struct script_builder script = {0};

  if (voco_write_frame_header(&frame, append_to_script, "ui", "setStatus", "A") &&
      voco_write_text_field(&frame, append_to_script, message) &&
      script_append(&script, "Voco.receive(") &&
      script_append_js_string(&script, frame.data) &&
      script_append(&script, ")")) {
    webview_eval(w, script.data);
  }

  free(frame.data);
  free(script.data);
}
```

In JavaScript, register a handler:

```javascript
window.Voco.on('ui', (command, type, payload) => {
    if (command === 'setStatus') {
        setStatus(payload[0] || '');
    }
});
```

## Packaging `voco.js`

During development, it is useful to keep `voco.js` as a separate source file:

```html
<script src="voco.js"></script>
<script>
  // application code
</script>
```

For packaged apps, choose one of two patterns:

- Copy `voco.js` beside `ui.html` and load it as a local resource.
- Inline `voco.js` into generated HTML during the build.

ro-totem uses both patterns:

- Linux and Windows-style embedded builds inline `voco.js` into
  `build/ui_embedded.html`.
- The macOS bundle copies `voco.js` to `Contents/Resources` beside `ui.html`.

The Makefile rule is deliberately simple: it replaces
`<script src="voco.js"></script>` with a literal inline `<script>` block.

## Error Handling

Voco rejects malformed frames by returning `0` in C or `null` / `false` in
JavaScript. It does not report detailed parse errors. The intended pattern is:

- reject malformed transport frames silently or log them in debug builds;
- validate command names and payload counts in the application dispatcher;
- validate numeric ranges before using values;
- decode text fields only when needed; and
- return operation-specific status messages through normal app callbacks.

## Limits

The current C implementation uses `VOCO_MAX_FIELDS`, defaulting to `80`, to keep
the parser bounded. Increase it if an application legitimately sends many
fields, or redesign that command to send a manifest payload.

Field lengths are parsed as `size_t`. Frames are still ordinary C strings, so
embedded NUL bytes are not supported. Binary payloads should be encoded as safe
text, for example base64, before being passed as a Voco field.

## What Voco Is Not

Voco is not:

- a security sandbox;
- a schema language;
- a promise or async task system;
- a binary IPC layer;
- a replacement for app-level validation; or
- a complete GUI framework.

It is a small, symmetric string framing layer for applications that already have
a webview string bridge and want a more formal contract than ad hoc prefixes or
delimiter-split messages.

## Extraction Checklist

To move Voco into its own repository:

1. Copy `voco.h` and `voco.js`.
2. Add a tiny example app showing `window.external.invoke()` and
   `Voco.receive(...)`.
3. Add tests for `Voco.format()` / `Voco.parse()`.
4. Add C tests for valid frames, malformed frames, percent decoding, empty
   fields, numeric parsing, and field-count limits.
5. Document build patterns for copied local resources and generated inline HTML.

