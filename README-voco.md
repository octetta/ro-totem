# Voco: Symmetric Loop Architecture for Legacy Webview Runtimes

Voco is an allocation-free, symmetric serialization protocol tailored for low-overhead native window processing loops. By eliminating JSON parsing and heavy RPC abstraction layers, Voco provides an exceptionally lean execution bridge across raw string wires. It explicitly targets single-header legacy runtime environments like the 2017 version of `webview.h`.

---

## Directory Blueprint

```text
voco/
├── CMakeLists.txt          # Cross-platform build configuration
├── README.md               # Architecture layout and documentation
├── src/
│   ├── main.c              # Core C engine and Voco parser
│   └── ui.html             # Unescaped HTML UI asset
└── vendor/
    └── webview/
        └── webview.h       # Single-header 2017 library (Serge Zaitsev)
```

---

## Architectural Principles

Modern desktop-web hybrid containers (e.g., Electron, Tauri) rely on complex asynchronous IPC serialization channels, often invoking heavy JSON stringification and context-switching overhead. Voco takes a completely different path optimized for high-throughput, low-latency data loops.

### 1. In-Place, Zero-Allocation Parsing
Voco treats the Webview string wire as a fixed, immutable transaction space. The native C parser (`voco_parse`) scans incoming byte strings via index pointers to extract boundaries sequentially. Instead of duplicating tokens or shifting buffers onto the heap, it records offsets directly from the original bridge pointer. This guarantees a **static $O(1)$ memory footprint**, completely eliminating heap churn and GC pauses during continuous serialization.

### 2. Symmetrical Data Framing
The frontend and backend use an identical data representation sequence. The pipeline converts untyped binary objects into predictable, base64-encoded safe text characters. This ensures that a single, unified parser logic path handles routing on both sides of the application wire.

### 3. Asynchronous Downstream Rehydration
When passing mutated states back down to the interface layer, the C engine avoids heavy execution loops by invoking an inline Javascript script evaluation (`webview_eval`). The browser frame catches this raw string, parses it using the exact same delimited rules, and safely rehydrates binary representations into local typed memory buffers.

---

## Targeted Use Cases

* **Digital Signal Processing (DSP) & Waveform Visualization:** Streaming high-rate audio buffers, synthesizer snapshot data, or multi-channel telemetry straight from a native processing thread into a high-speed browser `<canvas>` or WebGL rendering view. Voco maps binary data structures straight into Javascript `Float32Array` views with minimal latency.
* **Array-Oriented Language / REPL Frontends:** Providing a clean desktop shell interface for execution tools (such as minimal APL/K clones or macro engines) where the dominant transaction profiles consist of flat vector states, single-character execution variables, and mathematical expressions.
* **Resource-Constrained Tooling & Systems Programming:** Building configuration utilities, localized instrumentation monitors, or peripheral control interfaces that must run seamlessly on low-spec hardware without consuming hundreds of megabytes of baseline memory.

---

## The Protocol Layout Envelope

Both upstream (`JS -> C`) and downstream (`C -> JS`) messages share the identical symmetric signature structure:

```text
"vocab:command:type:payload"
```

* **`vocab`**: Core execution domain namespace (Max 63 bytes).
* **`command`**: Execution action identifier target (Max 63 bytes).
* **`type`**: Stream type token formatting guard (`A` = Raw ASCII string, `F` = Base64 Float32 Array).
* **`payload`**: The serialized array content block.

---

## Developer Integration Blueprint (Code Snippets)

Here is how the protocol maps directly into the application boundaries found in `ui.html` and `main.c`.

### 1. Upstream Transmission (JavaScript -> C)
To stream either plain-text expressions or high-performance binary vectors up to the native engine, format the envelope string and pass it to the webview wire invocation hook:

```javascript
// From src/ui.html
const Voco = {
  send: function(vocab, cmd, type, payload) {
    let serialized = payload;
    
    // If passing binary float data, encode to raw Base64 string first
    if (type === 'F' && payload instanceof Float32Array) {
      serialized = this._toBase64(new Uint8Array(payload.buffer));
    }
    
    // Construct delimited frame boundary
    const envelope = `${vocab}:${cmd}:${type}:${serialized}`;
    
    // Dispatch straight across the native 2017 runtime wire
    window.external.invoke(envelope);
  }
};

// Usage Examples:
Voco.send('dsl', 'eval', 'A', 'A: 10 * s ! 16');
Voco.send('dsp', 'load_samples', 'F', new Float32Array([1.0, -0.75, 0.5]));
```

### 2. Zero-Allocation In-Place Parsing (C Core)
The native application layer intercepts the message. Rather than calling a tokenizing parser that fragments memory, it maps pointers directly to tracking offsets inside the stack allocation:

```c
// From src/main.c
void on_voco_bridge_message(struct webview *w, const char *arg) {
    voco_msg_t msg;

    // Parses string in-place without generating heap allocations
    if (!voco_parse(arg, &msg)) {
        return; // Malformed transmission guard
    }

    // Route execution using the unpacked pointers
    if (strcmp(msg.vocab, "dsp") == 0 && strcmp(msg.cmd, "load_samples") == 0) {
        float vector_buffer[32];
        
        // Extract raw floats directly out of the Base64 payload offset
        size_t decoded_bytes = voco_b64_decode(msg.data, msg.data_len, (unsigned char *)vector_buffer);
        size_t sample_count = decoded_bytes / sizeof(float);
        
        // Mutate or process the raw array instantly on the metal...
    }
}
```

### 3. Downstream Dispatch & Catch Loop (C -> JavaScript)
To pass states symmetrically back down to the UI, the C layer serializes its results using the exact same structural signature format and drops it into an evaluation frame script call:

```c
// From src/main.c
// Re-encode processed native buffer back to safe text payload
char b64_out[1024];
voco_b64_encode((unsigned char *)vector_buffer, count * sizeof(float), b64_out);

// Push down via identical structural envelope syntax string
char js_dispatch[4096];
snprintf(js_dispatch, sizeof(js_dispatch), 
         "Voco.receive('dsp', 'render_wave', 'F', '%s');", b64_out);

webview_eval(w, js_dispatch);
```

The browser context intercepts this execution string via the corresponding callback router, handles the datatype casting seamlessly, and routes it to the DOM:

```javascript
// From src/ui.html
const Voco = {
  receive: function(vocab, cmd, type, payload) {
    let cleanPayload = payload;
    
    if (type === 'F') {
      // Rehydrate the incoming base64 string back into a native typed float vector
      const binaryStr = atob(payload);
      const bytes = new Uint8Array(binaryStr.length);
      for (let i = 0; i < binaryStr.length; i++) {
        bytes[i] = binaryStr.charCodeAt(i);
      }
      cleanPayload = new Float32Array(bytes.buffer);
    }

    // Dispatch cleanly to your layout rendering engine or graph view
    this._renderToTerminal(vocab, cmd, type, cleanPayload);
  }
};
```

---

## Compilation and Execution

### Canonical Build Pipeline (CMake)
```bash
mkdir build && cd build
cmake ..
cmake --build .
./voco_run
```

### Manual Linux Compilation (Direct Shell Command)
To compile manually without using the CMake build engine, you must explicitly call `xxd` to generate the file asset layout before compiling:

```bash
xxd -i src/ui.html src/ui_html.h
gcc src/main.c -o voco_run -Isrc -Ivendor/webview $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0)
./voco_run
```