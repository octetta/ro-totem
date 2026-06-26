#include <skred/api.h>
#include "rototem_version.h"
#include "voco.h"

#ifndef __APPLE__
#include "ui_html.h"
#endif

#define WEBVIEW_IMPLEMENTATION
/* Define WEBVIEW_WINAPI, WEBVIEW_GTK, or WEBVIEW_COCOA when compiling. */
#include "vendor/webview/webview.h"
#include "vendor/miniz/miniz.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
static int get_resource_path(const char *filename, char *out_path, size_t out_size) {
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
  char path[PATH_MAX];

  if (!resourcesURL) return 0;
  int found = CFURLGetFileSystemRepresentation(
    resourcesURL, true, (UInt8 *)path, sizeof(path));
  CFRelease(resourcesURL);
  if (!found) return 0;
  int written = snprintf(out_path, out_size, "%s/%s", path, filename);
  return written >= 0 && (size_t)written < out_size;
}
#else
#include <sys/wait.h>
static int get_resource_path(const char *filename, char *out_path, size_t out_size) {
  char path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len < 0 || (size_t)len >= sizeof(path) - 1) return 0;
  path[len] = '\0';
  char *last = strrchr(path, '/');
  if (!last) return 0;
  *last = '\0';
  int written = snprintf(out_path, out_size, "%s/%s", path, filename);
  return written >= 0 && (size_t)written < out_size;
}
#endif

static int file_url_byte_is_literal(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') ||
         byte == '/' || byte == ':' || byte == '-' ||
         byte == '_' || byte == '.' || byte == '~';
}

static int append_file_url_byte(
    char *url, size_t url_size, size_t *used, unsigned char byte) {
  static const char hex[] = "0123456789ABCDEF";
  int literal = file_url_byte_is_literal(byte);
  size_t needed = literal ? 1 : 3;
  if (*used + needed >= url_size) return 0;

  if (literal) {
    url[(*used)++] = (char)byte;
    return 1;
  }

  url[(*used)++] = '%';
  url[(*used)++] = hex[byte >> 4];
  url[(*used)++] = hex[byte & 0x0f];
  return 1;
}

static int make_file_url(const char *path, char *url, size_t url_size) {
  const char *prefix = "file://";
  size_t used = strlen(prefix);
  if (url_size <= used) return 0;
  memcpy(url, prefix, used);

  for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
    if (!append_file_url_byte(url, url_size, &used, *p)) return 0;
  }
  url[used] = '\0';
  return 1;
}

#ifndef __APPLE__
static int data_url_byte_is_literal(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') ||
         byte == '-' || byte == '_' || byte == '.' || byte == '~';
}

static char *make_embedded_ui_url(void) {
  static const char prefix[] = "data:text/html;charset=utf-8,";
  static const char hex[] = "0123456789ABCDEF";
  size_t prefix_length = sizeof(prefix) - 1;
  size_t encoded_length = prefix_length;

  for (unsigned int i = 0; i < rototem_ui_html_len; i++) {
    encoded_length += data_url_byte_is_literal(rototem_ui_html[i]) ? 1 : 3;
  }

  char *url = malloc(encoded_length + 1);
  if (!url) return NULL;

  memcpy(url, prefix, prefix_length);
  size_t used = prefix_length;
  for (unsigned int i = 0; i < rototem_ui_html_len; i++) {
    unsigned char byte = rototem_ui_html[i];
    if (data_url_byte_is_literal(byte)) {
      url[used++] = (char)byte;
    } else {
      url[used++] = '%';
      url[used++] = hex[byte >> 4];
      url[used++] = hex[byte & 0x0f];
    }
  }
  url[used] = '\0';
  return url;
}
#endif

struct script_builder {
  char *data;
  size_t length;
  size_t capacity;
};

static int script_reserve(struct script_builder *script, size_t extra) {
  if (script->length == SIZE_MAX ||
      extra > SIZE_MAX - script->length - 1) return 0;
  size_t required = script->length + extra + 1;
  if (required <= script->capacity) return 1;

  size_t capacity = script->capacity ? script->capacity : 128;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2) {
      capacity = required;
      break;
    }
    capacity *= 2;
  }

  char *data = realloc(script->data, capacity);
  if (!data) return 0;
  script->data = data;
  script->capacity = capacity;
  return 1;
}

static int script_append_n(
    struct script_builder *script, const char *text, size_t length) {
  if (!script_reserve(script, length)) return 0;
  memcpy(script->data + script->length, text, length);
  script->length += length;
  script->data[script->length] = '\0';
  return 1;
}

static int script_append(struct script_builder *script, const char *text) {
  return script_append_n(script, text, strlen(text));
}

static int script_appendf(struct script_builder *script, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0 || !script_reserve(script, (size_t)length)) {
    va_end(args);
    return 0;
  }
  vsnprintf(
    script->data + script->length,
    script->capacity - script->length,
    format,
    args);
  va_end(args);
  script->length += (size_t)length;
  return 1;
}

static int script_append_js_byte(
    struct script_builder *script, unsigned char byte) {
  switch (byte) {
    case '\\': return script_append(script, "\\\\");
    case '\'': return script_append(script, "\\'");
    case '\n': return script_append(script, "\\n");
    case '\r': return script_append(script, "\\r");
    case '\t': return script_append(script, "\\t");
    default:
      if (byte < 0x20) return script_appendf(script, "\\u%04x", byte);
      return script_append_n(script, (const char *)&byte, 1);
  }
}

static int script_append_js_string(
    struct script_builder *script, const char *value) {
  if (!value) value = "";
  if (!script_append(script, "'")) return 0;
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    if (!script_append_js_byte(script, *p)) return 0;
  }
  return script_append(script, "'");
}

static void script_eval(struct webview *w, struct script_builder *script) {
  if (script->data) webview_eval(w, script->data);
  free(script->data);
  script->data = NULL;
  script->length = 0;
  script->capacity = 0;
}

static int script_append_voco(
    void *context, const char *text, size_t length) {
  return script_append_n((struct script_builder *)context, text, length);
}

static void voco_eval_frame(struct webview *w, const char *frame) {
  struct script_builder script = {0};

  if (script_append(&script, "Voco.receive(") &&
      script_append_js_string(&script, frame) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
}

static int voco_append_frame_header(
    struct script_builder *frame,
    const char *vocab,
    const char *command,
    const char *type) {
  return voco_write_frame_header(
      frame, script_append_voco, vocab, command, type);
}

static void voco_send_fields(
    struct webview *w,
    const char *vocab,
    const char *command,
    const char *type,
    const char *const *payloads,
    int payload_count) {
  struct script_builder frame = {0};
  int ok = voco_append_frame_header(&frame, vocab, command, type);

  for (int i = 0; ok && i < payload_count; i++) {
    ok = voco_write_text_field(&frame, script_append_voco, payloads[i]);
  }
  if (ok) voco_eval_frame(w, frame.data);
  free(frame.data);
}

static void voco_send(
    struct webview *w,
    const char *vocab,
    const char *command,
    const char *type,
    const char *payload) {
  const char *payloads[] = {payload};
  voco_send_fields(w, vocab, command, type, payloads, 1);
}

static void addLog(struct webview *w, const char *message);

struct scope_ipc_reader {
  int fd;
  int pad;
  uint64_t size;
  void *mapping;
  float *frames;
  char name[128];
};

#if defined(__GNUC__) && !defined(_WIN32)
#if defined(__APPLE__)
#define ROTOTEM_WEAK_SCOPE __attribute__((weak_import))
#else
#define ROTOTEM_WEAK_SCOPE __attribute__((weak))
#endif
extern int scope_ipc_reader_open(
    struct scope_ipc_reader *reader, const char *name) ROTOTEM_WEAK_SCOPE;
extern int scope_ipc_reader_latest(
    struct scope_ipc_reader *reader, float *frames, uint32_t max_frames,
    uint64_t *first_frame) ROTOTEM_WEAK_SCOPE;
extern void scope_ipc_reader_close(
    struct scope_ipc_reader *reader) ROTOTEM_WEAK_SCOPE;
#else
static int scope_ipc_reader_open(
    struct scope_ipc_reader *reader, const char *name) {
  (void)reader;
  (void)name;
  return -1;
}

static int scope_ipc_reader_latest(
    struct scope_ipc_reader *reader, float *frames, uint32_t max_frames,
    uint64_t *first_frame) {
  (void)reader;
  (void)frames;
  (void)max_frames;
  (void)first_frame;
  return -1;
}

static void scope_ipc_reader_close(struct scope_ipc_reader *reader) {
  (void)reader;
}
#endif

#define VISUAL_SCOPE_NAME "skred-scope"
#define VISUAL_SCOPE_CHANNELS 10
#define VISUAL_SCOPE_MASK ((1u << VISUAL_SCOPE_CHANNELS) - 1u)
#define VISUAL_SCOPE_FRAMES 1024
#define VISUAL_SCOPE_POINTS 240

static struct scope_ipc_reader visual_scope_reader;
static int visual_scope_publishing = 0;
static int visual_scope_open = 0;
static float visual_scope_frames[VISUAL_SCOPE_FRAMES * VISUAL_SCOPE_CHANNELS];

static void send_visual_scope_status(
    struct webview *w, const char *message, int running) {
  voco_send(w, "scope", running ? "status-running" : "status-stopped",
      "A", message);
}

static int visual_scope_reader_available(void) {
#if defined(__GNUC__) && !defined(_WIN32)
  return scope_ipc_reader_open && scope_ipc_reader_latest &&
      scope_ipc_reader_close;
#else
  return 0;
#endif
}

static void close_visual_scope_reader(void) {
  if (visual_scope_open && visual_scope_reader_available()) {
    scope_ipc_reader_close(&visual_scope_reader);
  }
  memset(&visual_scope_reader, 0, sizeof(visual_scope_reader));
  visual_scope_open = 0;
}

static int ensure_visual_scope(struct webview *w) {
  if (!visual_scope_reader_available()) {
    send_visual_scope_status(
        w, "Scope reader is unavailable in this Skred build.", 0);
    return 0;
  }

  if (!visual_scope_publishing) {
    int result = skred_scope_start(VISUAL_SCOPE_NAME, VISUAL_SCOPE_MASK, 1.0);
    if (result != 0) {
      send_visual_scope_status(
          w, "Could not start Skred scope publisher.", 0);
      return 0;
    }
    visual_scope_publishing = 1;
  }

  if (!visual_scope_open) {
    if (scope_ipc_reader_open(&visual_scope_reader, VISUAL_SCOPE_NAME) != 0) {
      send_visual_scope_status(w, "Could not open Skred scope reader.", 0);
      return 0;
    }
    visual_scope_open = 1;
  }

  return 1;
}

static int append_visual_float(struct script_builder *script, float value) {
  if (value > -0.000001f && value < 0.000001f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  if (value < -1.0f) value = -1.0f;
  return script_appendf(script, "%.5g", (double)value);
}

static int append_visual_scope_points(
    struct script_builder *script, const float *frames, int frame_count) {
  int points = frame_count < VISUAL_SCOPE_POINTS
      ? frame_count
      : VISUAL_SCOPE_POINTS;
  if (!script_append(script, "[")) return 0;
  for (int point = 0; point < points; point++) {
    int start = (int)(((int64_t)point * frame_count) / points);
    int end = (int)(((int64_t)(point + 1) * frame_count) / points);
    if (end <= start) end = start + 1;

    float sum = 0.0f;
    for (int i = start; i < end; i++) {
      const float *frame = frames + (i * VISUAL_SCOPE_CHANNELS);
      sum += (frame[0] + frame[1]) * 0.5f;
    }
    float value = sum / (float)(end - start);
    if (point > 0 && !script_append(script, ",")) return 0;
    if (!append_visual_float(script, value)) return 0;
  }
  return script_append(script, "]");
}

static int append_visual_scope_peaks(
    struct script_builder *script, const float *frames, int frame_count) {
  if (!script_append(script, "[")) return 0;
  for (int channel = 0; channel < VISUAL_SCOPE_CHANNELS; channel++) {
    float peak = 0.0f;
    for (int i = 0; i < frame_count; i++) {
      float value = frames[i * VISUAL_SCOPE_CHANNELS + channel];
      if (value < 0.0f) value = -value;
      if (value > peak) peak = value;
    }
    if (channel > 0 && !script_append(script, ",")) return 0;
    if (!append_visual_float(script, peak)) return 0;
  }
  return script_append(script, "]");
}

static void poll_visual_scope(struct webview *w) {
  if (!ensure_visual_scope(w)) return;

  uint64_t first_frame = 0;
  int frames = scope_ipc_reader_latest(
      &visual_scope_reader, visual_scope_frames, VISUAL_SCOPE_FRAMES,
      &first_frame);
  if (frames < 0) {
    close_visual_scope_reader();
    send_visual_scope_status(w, "Scope read failed.", 0);
    return;
  }
  if (frames == 0) {
    send_visual_scope_status(w, "Waiting for scope frames...", 1);
    return;
  }

  struct script_builder frame = {0};
  struct script_builder waveform = {0};
  struct script_builder peaks = {0};
  if (append_visual_scope_points(&waveform, visual_scope_frames, frames) &&
      append_visual_scope_peaks(&peaks, visual_scope_frames, frames) &&
      script_append(&frame, VOCO_MAGIC) &&
      voco_write_text_field(&frame, script_append_voco, "scope") &&
      voco_write_text_field(&frame, script_append_voco, "data") &&
      voco_write_text_field(&frame, script_append_voco, "N") &&
      voco_write_int_field(&frame, script_append_voco, frames) &&
      voco_write_u64_field(&frame, script_append_voco, (unsigned long long)first_frame) &&
      voco_write_int_field(&frame, script_append_voco, VISUAL_SCOPE_CHANNELS) &&
      voco_write_text_field(&frame, script_append_voco, waveform.data) &&
      voco_write_text_field(&frame, script_append_voco, peaks.data)) {
    voco_eval_frame(w, frame.data);
  } else {
    send_visual_scope_status(w, "Not enough memory to send scope data.", 0);
  }
  free(frame.data);
  free(waveform.data);
  free(peaks.data);
}

static void stop_visual_scope(void) {
  close_visual_scope_reader();
  if (visual_scope_publishing) {
    skred_scope_stop();
    visual_scope_publishing = 0;
  }
}

static int handle_audio_command(const char *line) {
  int result = skred_audio_command(line);
  if (result == 0) return 0;
  const char *message = skred_audio_message();
  if (message && message[0]) printf("%s\n", message);
  return 1;
}

static char *skoder(const char *msg) {
  char *log = "";
  if (handle_audio_command(msg)) {
    log = skred_audio_message();
  } else {
    skred_command((char *)msg);
    log = skred_log();
  }
  return log;
}

static void addSkodeLog(struct webview *w, const char *log) {
  if (log && log[0] != '\0') {
    size_t length = strlen(log);
    char *copy = malloc(length + 1);
    if (!copy) {
      addLog(w, log);
      return;
    }
    memcpy(copy, log, length + 1);

    char *end;
    char *s = copy;
    while ((end = strchr(s, '\n'))) {
      *end = '\0';
      if (end > s && end[-1] == '\r') end[-1] = '\0';
      addLog(w, s);
      s = end + 1;
    }
    if (*s) addLog(w, s);
    free(copy);
  }
}

static int wavepointer = 300;

/*
 * ui.html normally sends Voco frames:
 *
 *   V1<len:native><len:command><len:type><len:payload>...
 *
 * The legacy compact envelope below remains as a compatibility fallback:
 *
 *   !<commands>       Pass compact commands to the audio engine.
 *   @                 Open the wave-directory chooser.
 *   >v<voice>         Choose a wave for a stereo voice pair.
 *   R<directory>      Scan a directory and return its .wav filenames.
 *   W<voice>:<path>   Load a wave into voice and voice + 1.
 *   K<index>          Choose a managed project file.
 *   JS<json> / JL     Save or load settings.
 *   PB / PW / PX / PF / PL Save or load a project ZIP.
 *   DR / DA<c>:<n>    Refresh or apply an audio-device selection.
 *   G<width>:<height> Resize the native main window content area.
 *   VS / VP / VT      Start, poll, or stop the visual scope bridge.
 *
 * The payload after '!' belongs to the audio engine, not this dispatcher.
 * Common engine forms are v<n>a<x> (volume), v<n>n<x> (speed),
 * v<n>l<0|1> (play), and v<n>m<0|1> (mute). Direction commands are
 * v<n>b0 (forward), v<n>b1 (backward), and v<n>b- (invert). Prefer b-
 * over bare b because b consumes a numeric parameter and can otherwise
 * absorb part of the command that follows it.
 * When extending the protocol, add a named UI bridge method and a named
 * enum value or subcommand here so raw prefixes stay in one place.
 */
enum native_command {
  NATIVE_ENGINE_COMMAND = '!',
  NATIVE_CHOOSE_DIRECTORY = '@',
  NATIVE_AUDIO_DEVICE = 'D',
  NATIVE_SETTINGS = 'J',
  NATIVE_PROJECT = 'P',
  NATIVE_WINDOW_GEOMETRY = 'G',
  NATIVE_VISUAL_SCOPE = 'V',
  NATIVE_CHOOSE_PROJECT_FILE = 'K',
  NATIVE_LOAD_DIRECTORY = 'R',
  NATIVE_LOAD_WAVE = 'W',
  NATIVE_CHOOSE_WAVE = '>'
};

static void load_wave_directory(struct webview *w, const char *dirname) {
  voco_send(w, "ui", "clearWaveFiles", "A", "");
  voco_send(w, "ui", "setWaveDirectory", "A", dirname);

  DIR *dp = opendir(dirname);
  if (!dp) return;

  struct dirent *entry;
  while ((entry = readdir(dp))) {
    const char *name = entry->d_name;
    size_t len = strlen(name);
    if (len <= 4 || strcasecmp(name + len - 4, ".wav") != 0) continue;
    voco_send(w, "ui", "addWaveFile", "A", name);
  }
  closedir(dp);
}

static void addLog(struct webview *w, const char *message) {
  voco_send(w, "ui", "addLog", "A", message);
}

static const char *path_basename(const char *path) {
  const char *basename = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\') basename = p + 1;
  }
  return basename;
}

static int allocate_wave(void) {
  int wave = wavepointer++;
  if (wavepointer > 998) wavepointer = 0;
  return wave;
}

static int load_wave_channel(
    struct webview *w, const char *filename, const char *shortname,
    int voice, int channel) {
  char cmd[PATH_MAX + 128];
  int wave = allocate_wave();
  int pan = channel == 0 ? -1 : 1;

  snprintf(cmd, sizeof(cmd),
    "[%s] /ws%d %d v%d p%d w%d a0 B1 f440 t1 0 1 1",
    filename, wave, channel, voice + channel, pan, wave);
  addLog(w, cmd);
  addSkodeLog(w, skoder(cmd));

  snprintf(cmd, sizeof(cmd), "[%s] wt %d", shortname, wave);
  addSkodeLog(w, skoder(cmd));
  return wave;
}

static void notify_wave_loaded(
    struct webview *w, const char *filename, const char *shortname,
    int voice, const int waves[2]) {
  char voice_text[32];
  char left_text[32];
  char right_text[32];
  snprintf(voice_text, sizeof(voice_text), "%d", voice);
  snprintf(left_text, sizeof(left_text), "%d", waves[0]);
  snprintf(right_text, sizeof(right_text), "%d", waves[1]);
  const char *payloads[] = {
    voice_text, filename, shortname, left_text, right_text
  };
  voco_send_fields(w, "ui", "setTrackWave", "N", payloads, 5);
}

static void apply_loaded_wave_controls(struct webview *w, int voice) {
  char voice_text[32];
  snprintf(voice_text, sizeof(voice_text), "%d", voice);
  voco_send(w, "ui", "applyTrackControls", "N", voice_text);
  voco_send(w, "ui", "waveFileLoaded", "A", "");
}

static void load_wave_file(struct webview *w, const char *filename, int voice) {
  if (!filename[0] || voice < 0 || voice + 1 >= 32) return;

  const char *shortname = path_basename(filename);
  int waves[2];
  for (int channel = 0; channel < 2; channel++) {
    waves[channel] =
      load_wave_channel(w, filename, shortname, voice, channel);
  }

  notify_wave_loaded(w, filename, shortname, voice, waves);
  apply_loaded_wave_controls(w, voice);
}

static void load_settings_file(struct webview *w, const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    voco_send(w, "ui", "settingsFileError", "A",
        "Could not open settings file.");
    return;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    voco_send(w, "ui", "settingsFileError", "A",
        "Could not read settings file.");
    return;
  }

  long length = ftell(file);
  if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    voco_send(w, "ui", "settingsFileError", "A",
        "Settings file is too large or invalid.");
    return;
  }

  char *json = malloc((size_t)length + 1);
  if (!json) {
    free(json);
    fclose(file);
    voco_send(w, "ui", "settingsFileError", "A",
        "Not enough memory to load settings.");
    return;
  }

  size_t read = fread(json, 1, (size_t)length, file);
  int read_error = ferror(file);
  fclose(file);
  if (read != (size_t)length || read_error) {
    free(json);
    voco_send(w, "ui", "settingsFileError", "A",
        "Could not read settings file.");
    return;
  }
  json[read] = '\0';

  voco_send(w, "ui", "loadSettingsFromText", "J", json);
  free(json);
}

static void save_settings_file(struct webview *w, const char *json) {
  char filename[PATH_MAX] = "";
  webview_dialog(w, WEBVIEW_DIALOG_TYPE_SAVE, WEBVIEW_DIALOG_FLAG_FILE,
                 "Save settings", "ro-totem-settings.json",
                 filename, sizeof(filename));
  if (!filename[0]) return;

  size_t length = strlen(filename);
  if (length < 5 || strcasecmp(filename + length - 5, ".json") != 0) {
    if (length + 5 >= sizeof(filename)) {
      voco_send(w, "ui", "settingsFileError", "A",
          "Settings filename is too long.");
      return;
    }
    memcpy(filename + length, ".json", 6);
  }

  FILE *file = fopen(filename, "wb");
  if (!file) {
    voco_send(w, "ui", "settingsFileError", "A",
        "Could not create settings file.");
    return;
  }

  size_t json_length = strlen(json);
  int ok = fwrite(json, 1, json_length, file) == json_length;
  ok = fclose(file) == 0 && ok;
  if (ok) {
    voco_send(w, "ui", "settingsFileSaved", "A", "");
  } else {
    voco_send(w, "ui", "settingsFileError", "A",
        "Could not write settings file.");
  }
}

#define PROJECT_MAX_WAVES 32
#define PROJECT_MAX_FILES 64
#define PROJECT_MAX_SETTINGS_SIZE (1024 * 1024)
#define PROJECT_MAX_WAVE_SIZE ((mz_uint64)1024 * 1024 * 1024)
#define PROJECT_MAX_TOTAL_WAVE_SIZE ((mz_uint64)4 * 1024 * 1024 * 1024)
#define PROJECT_MAX_FILE_SIZE ((mz_uint64)64 * 1024 * 1024)
#define PROJECT_MAX_TOTAL_FILE_SIZE ((mz_uint64)512 * 1024 * 1024)

struct project_writer {
  mz_zip_archive archive;
  char filename[PATH_MAX];
  char temp_filename[PATH_MAX];
  int active;
  int failed;
};

static struct project_writer project_writer;
static char project_temp_directory[PATH_MAX];
static char pending_project_temp_directory[PATH_MAX];

static void project_file_error(struct webview *w, const char *message) {
  voco_send(w, "ui", "settingsFileError", "A", message);
}

static int append_filename_extension(
    char *filename, size_t filename_size, const char *extension) {
  size_t length = strlen(filename);
  size_t extension_length = strlen(extension);
  if (length >= extension_length &&
      strcasecmp(filename + length - extension_length, extension) == 0) {
    return 1;
  }
  if (length + extension_length >= filename_size) return 0;
  memcpy(filename + length, extension, extension_length + 1);
  return 1;
}

static void discard_project_writer(int remove_partial_file) {
  if (!project_writer.active) return;
  mz_zip_writer_end(&project_writer.archive);
  project_writer.active = 0;
  if (remove_partial_file && project_writer.temp_filename[0]) {
    remove(project_writer.temp_filename);
  }
}

static int replace_file(const char *source, const char *destination) {
#ifdef _WIN32
  return MoveFileExA(
    source, destination,
    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(source, destination) == 0;
#endif
}

static void begin_project_save(struct webview *w) {
  discard_project_writer(1);
  memset(&project_writer, 0, sizeof(project_writer));

  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_SAVE, WEBVIEW_DIALOG_FLAG_FILE,
    "Save project", "ro-totem-project.zip",
    project_writer.filename, sizeof(project_writer.filename));
  if (!project_writer.filename[0]) {
    voco_send(w, "ui", "projectFileCancelled", "A", "");
    return;
  }
  if (!append_filename_extension(
        project_writer.filename, sizeof(project_writer.filename), ".zip")) {
    project_file_error(w, "Project filename is too long.");
    return;
  }
  int written = snprintf(
    project_writer.temp_filename, sizeof(project_writer.temp_filename),
    "%s.ro-totem-tmp", project_writer.filename);
  if (written < 0 ||
      (size_t)written >= sizeof(project_writer.temp_filename)) {
    project_file_error(w, "Project filename is too long.");
    return;
  }
  remove(project_writer.temp_filename);

  memset(&project_writer.archive, 0, sizeof(project_writer.archive));
  if (!mz_zip_writer_init_file(
        &project_writer.archive, project_writer.temp_filename, 0)) {
    project_file_error(w, "Could not create project archive.");
    remove(project_writer.temp_filename);
    return;
  }
  project_writer.active = 1;
  voco_send(w, "ui", "projectSaveReady", "A", "");
}

static int parse_project_file_argument(
    const char *arg, int max_files, int *index, char *archive_name,
    size_t archive_name_size, const char **filename) {
  char *end;
  long parsed = strtol(arg, &end, 10);
  if (end == arg || *end != ':' ||
      parsed < 0 || parsed >= max_files) {
    return 0;
  }

  const char *archive_start = end + 1;
  const char *archive_end = strchr(archive_start, ':');
  if (!archive_end || archive_end == archive_start || !archive_end[1]) return 0;
  size_t length = (size_t)(archive_end - archive_start);
  if (length >= archive_name_size) return 0;
  memcpy(archive_name, archive_start, length);
  archive_name[length] = '\0';

  *index = (int)parsed;
  *filename = archive_end + 1;
  return 1;
}

static int valid_project_wave_name(const char *name) {
  static const char prefix[] = "waves/";
  size_t prefix_length = sizeof(prefix) - 1;
  if (strncmp(name, prefix, prefix_length) != 0) return 0;

  const char *basename = name + prefix_length;
  size_t length = strlen(basename);
  if (length <= 4 ||
      strcasecmp(basename + length - 4, ".wav") != 0 ||
      strcmp(basename, ".") == 0 || strcmp(basename, "..") == 0) {
    return 0;
  }
  for (const unsigned char *p = (const unsigned char *)basename; *p; p++) {
    if (*p == '/' || *p == '\\' || *p == ':' || *p == '<' || *p == '>' ||
        *p == '"' || *p == '|' || *p == '?' || *p == '*' ||
        *p < 0x20 || *p == 0x7f) {
      return 0;
    }
  }
  return 1;
}

static int valid_project_file_name(const char *name) {
  static const char prefix[] = "files/";
  size_t prefix_length = sizeof(prefix) - 1;
  if (strncmp(name, prefix, prefix_length) != 0) return 0;

  const char *basename = name + prefix_length;
  size_t length = strlen(basename);
  if (!basename[0] ||
      length > 240 || basename[length - 1] == '.' || basename[length - 1] == ' ' ||
      strcmp(basename, ".") == 0 || strcmp(basename, "..") == 0) {
    return 0;
  }
  char stem[16];
  size_t stem_length = strcspn(basename, ".");
  if (stem_length < sizeof(stem)) {
    memcpy(stem, basename, stem_length);
    stem[stem_length] = '\0';
    if (strcasecmp(stem, "CON") == 0 || strcasecmp(stem, "PRN") == 0 ||
        strcasecmp(stem, "AUX") == 0 || strcasecmp(stem, "NUL") == 0 ||
        (stem_length == 4 &&
         (strncasecmp(stem, "COM", 3) == 0 ||
          strncasecmp(stem, "LPT", 3) == 0) &&
         stem[3] >= '1' && stem[3] <= '9')) {
      return 0;
    }
  }
  for (const unsigned char *p = (const unsigned char *)basename; *p; p++) {
    if (*p == '/' || *p == '\\' || *p == ':' || *p == '<' || *p == '>' ||
        *p == '"' || *p == '|' || *p == '?' || *p == '*' ||
        *p < 0x20 || *p == 0x7f) {
      return 0;
    }
  }
  return 1;
}

static void add_project_wave_values(
    struct webview *w, const char *archive_name, const char *filename) {
  if (!project_writer.active || project_writer.failed) return;
  if (!valid_project_wave_name(archive_name)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not save a WAV with an invalid path.");
    return;
  }

  if (!mz_zip_writer_add_file(
        &project_writer.archive, archive_name, filename,
        NULL, 0, MZ_NO_COMPRESSION)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not add a WAV file to the project archive.");
  }
}

static void add_project_file_values(
    struct webview *w, const char *archive_name, const char *filename) {
  if (!project_writer.active || project_writer.failed) return;
  if (!valid_project_file_name(archive_name)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not save a managed file with an invalid path.");
    return;
  }

  if (!mz_zip_writer_add_file(
        &project_writer.archive, archive_name, filename,
        NULL, 0, MZ_BEST_COMPRESSION)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not add a managed file to the project archive.");
  }
}

static void add_project_wave(struct webview *w, const char *arg) {
  int index;
  char archive_name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  const char *filename;
  if (!project_writer.active || project_writer.failed) return;
  if (!parse_project_file_argument(
        arg, PROJECT_MAX_WAVES, &index,
        archive_name, sizeof(archive_name), &filename) ||
      !valid_project_wave_name(archive_name)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not save a WAV with an invalid path.");
    return;
  }
  (void)index;
  add_project_wave_values(w, archive_name, filename);
}

static void add_project_file(struct webview *w, const char *arg) {
  int index;
  char archive_name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  const char *filename;
  if (!project_writer.active || project_writer.failed) return;
  if (!parse_project_file_argument(
        arg, PROJECT_MAX_FILES, &index,
        archive_name, sizeof(archive_name), &filename) ||
      !valid_project_file_name(archive_name)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not save a managed file with an invalid path.");
    return;
  }
  (void)index;
  add_project_file_values(w, archive_name, filename);
}

static void finish_project_save(struct webview *w, const char *json) {
  if (!project_writer.active) return;

  int ok = !project_writer.failed &&
    strlen(json) <= PROJECT_MAX_SETTINGS_SIZE &&
    mz_zip_writer_add_mem(
      &project_writer.archive, "settings.json", json, strlen(json),
      MZ_BEST_COMPRESSION) &&
    mz_zip_writer_finalize_archive(&project_writer.archive);
  ok = mz_zip_writer_end(&project_writer.archive) && ok;
  project_writer.active = 0;

  if (!ok || !replace_file(
        project_writer.temp_filename, project_writer.filename)) {
    remove(project_writer.temp_filename);
    project_file_error(w, "Could not finish writing the project archive.");
    return;
  }
  voco_send(w, "ui", "projectFileSaved", "A", project_writer.filename);
}

static int make_directory(const char *path) {
#ifdef _WIN32
  return _mkdir(path) == 0;
#else
  return mkdir(path, 0700) == 0;
#endif
}

static int remove_directory(const char *path) {
#ifdef _WIN32
  return _rmdir(path);
#else
  return rmdir(path);
#endif
}

static int join_path(
    char *path, size_t path_size, const char *directory, const char *name) {
  int written = snprintf(path, path_size, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < path_size;
}

static void cleanup_project_directory(const char *directory) {
  if (!directory || !directory[0]) return;

  const char *subdirectories[] = {"waves", "files"};
  for (size_t i = 0;
       i < sizeof(subdirectories) / sizeof(subdirectories[0]); i++) {
    char subdirectory[PATH_MAX];
    if (!join_path(
          subdirectory, sizeof(subdirectory),
          directory, subdirectories[i])) {
      continue;
    }

    DIR *dp = opendir(subdirectory);
    if (dp) {
      struct dirent *entry;
      while ((entry = readdir(dp))) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
          continue;
        }
        char filename[PATH_MAX];
        if (join_path(
              filename, sizeof(filename), subdirectory, entry->d_name)) {
          remove(filename);
        }
      }
      closedir(dp);
    }
    remove_directory(subdirectory);
  }
  remove_directory(directory);
}

static int create_project_temp_directory(char *path, size_t path_size) {
#ifdef _WIN32
  char temp_path[MAX_PATH];
  char temp_file[MAX_PATH];
  if (!GetTempPathA(sizeof(temp_path), temp_path) ||
      !GetTempFileNameA(temp_path, "rot", 0, temp_file)) {
    return 0;
  }
  DeleteFileA(temp_file);
  if (!CreateDirectoryA(temp_file, NULL)) return 0;
  int written = snprintf(path, path_size, "%s", temp_file);
  return written >= 0 && (size_t)written < path_size;
#else
  const char *temp_root = getenv("TMPDIR");
  if (!temp_root || !temp_root[0]) temp_root = "/tmp";
  int written = snprintf(
    path, path_size, "%s/ro-totem-project-XXXXXX", temp_root);
  if (written < 0 || (size_t)written >= path_size) return 0;
  return mkdtemp(path) != NULL;
#endif
}

struct project_archive_contents {
  int settings_index;
  int wave_count;
  struct {
    int file_index;
    char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  } waves[PROJECT_MAX_WAVES];
  int file_count;
  struct {
    int file_index;
    char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  } files[PROJECT_MAX_FILES];
};

static int inspect_project_archive(
    mz_zip_archive *archive, struct project_archive_contents *contents) {
  mz_uint file_count = mz_zip_reader_get_num_files(archive);
  if (file_count == 0 ||
      file_count > PROJECT_MAX_WAVES + PROJECT_MAX_FILES + 1) {
    return 0;
  }

  memset(contents, 0, sizeof(*contents));
  contents->settings_index = -1;

  mz_uint64 total_wave_size = 0;
  mz_uint64 total_file_size = 0;
  for (mz_uint i = 0; i < file_count; i++) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive, i, &stat) ||
        mz_zip_reader_is_file_a_directory(archive, i)) {
      return 0;
    }

    if (strcmp(stat.m_filename, "settings.json") == 0) {
      if (contents->settings_index >= 0 ||
          stat.m_uncomp_size > PROJECT_MAX_SETTINGS_SIZE) {
        return 0;
      }
      contents->settings_index = (int)i;
      continue;
    }

    if (valid_project_wave_name(stat.m_filename)) {
      if (contents->wave_count >= PROJECT_MAX_WAVES ||
          stat.m_uncomp_size > PROJECT_MAX_WAVE_SIZE ||
          total_wave_size > PROJECT_MAX_TOTAL_WAVE_SIZE - stat.m_uncomp_size) {
        return 0;
      }
      for (int j = 0; j < contents->wave_count; j++) {
        if (strcasecmp(contents->waves[j].name, stat.m_filename) == 0) return 0;
      }
      contents->waves[contents->wave_count].file_index = (int)i;
      snprintf(
        contents->waves[contents->wave_count].name,
        sizeof(contents->waves[contents->wave_count].name),
        "%s", stat.m_filename);
      contents->wave_count++;
      total_wave_size += stat.m_uncomp_size;
      continue;
    }

    if (!valid_project_file_name(stat.m_filename) ||
        contents->file_count >= PROJECT_MAX_FILES ||
        stat.m_uncomp_size > PROJECT_MAX_FILE_SIZE ||
        total_file_size > PROJECT_MAX_TOTAL_FILE_SIZE - stat.m_uncomp_size) {
      return 0;
    }
    for (int j = 0; j < contents->file_count; j++) {
      if (strcasecmp(contents->files[j].name, stat.m_filename) == 0) return 0;
    }
    contents->files[contents->file_count].file_index = (int)i;
    snprintf(
      contents->files[contents->file_count].name,
      sizeof(contents->files[contents->file_count].name),
      "%s", stat.m_filename);
    contents->file_count++;
    total_file_size += stat.m_uncomp_size;
  }
  return contents->settings_index >= 0;
}

static int extract_project_waves(
    mz_zip_archive *archive, const struct project_archive_contents *contents,
    const char *directory) {
  char waves_directory[PATH_MAX];
  if (!join_path(
        waves_directory, sizeof(waves_directory), directory, "waves") ||
      !make_directory(waves_directory)) {
    return 0;
  }

  for (int i = 0; i < contents->wave_count; i++) {
    char filename[PATH_MAX];
    const char *basename = contents->waves[i].name + strlen("waves/");
    if (!join_path(filename, sizeof(filename), waves_directory, basename) ||
        !mz_zip_reader_extract_to_file(
          archive, (mz_uint)contents->waves[i].file_index, filename, 0)) {
      return 0;
    }
  }
  return 1;
}

static int extract_project_files(
    mz_zip_archive *archive, const struct project_archive_contents *contents,
    const char *directory) {
  if (contents->file_count == 0) return 1;

  char files_directory[PATH_MAX];
  if (!join_path(
        files_directory, sizeof(files_directory), directory, "files") ||
      !make_directory(files_directory)) {
    return 0;
  }

  for (int i = 0; i < contents->file_count; i++) {
    char filename[PATH_MAX];
    const char *basename = contents->files[i].name + strlen("files/");
    if (!join_path(filename, sizeof(filename), files_directory, basename) ||
        !mz_zip_reader_extract_to_file(
          archive, (mz_uint)contents->files[i].file_index, filename, 0)) {
      return 0;
    }
  }
  return 1;
}

static void accept_loaded_project(void);
static void reject_loaded_project(void);

static void load_project_file(struct webview *w, const char *filename) {
  mz_zip_archive archive;
  memset(&archive, 0, sizeof(archive));
  if (!mz_zip_reader_init_file(&archive, filename, 0)) {
    project_file_error(w, "Could not open project archive.");
    return;
  }

  struct project_archive_contents contents;
  if (!mz_zip_validate_archive(&archive, 0) ||
      !inspect_project_archive(&archive, &contents)) {
    mz_zip_reader_end(&archive);
    project_file_error(w, "This is not a valid ro-totem project archive.");
    return;
  }

  size_t json_size = 0;
  void *json_data = mz_zip_reader_extract_to_heap(
    &archive, (mz_uint)contents.settings_index, &json_size, 0);
  char *json = malloc(json_size + 1);
  char temp_directory[PATH_MAX] = "";
  int ok = json_data && json &&
    create_project_temp_directory(temp_directory, sizeof(temp_directory));
  if (ok) {
    memcpy(json, json_data, json_size);
    json[json_size] = '\0';
    ok = extract_project_waves(&archive, &contents, temp_directory) &&
      extract_project_files(&archive, &contents, temp_directory);
  }
  mz_free(json_data);
  mz_zip_reader_end(&archive);

  if (!ok) {
    free(json);
    cleanup_project_directory(temp_directory);
    project_file_error(w, "Could not extract the project archive.");
    return;
  }

  cleanup_project_directory(pending_project_temp_directory);
  snprintf(
    pending_project_temp_directory, sizeof(pending_project_temp_directory),
    "%s", temp_directory);

  struct script_builder frame = {0};
  int manifest_ok = 0;
  if (voco_append_frame_header(&frame, "ui", "loadProjectFromText", "J") &&
      voco_write_text_field(&frame, script_append_voco, json) &&
      voco_write_text_field(&frame, script_append_voco, pending_project_temp_directory)) {
    manifest_ok = 1;
    for (int i = 0; i < contents.file_count; i++) {
      const char *basename = contents.files[i].name + strlen("files/");
      if (!voco_write_text_field(&frame, script_append_voco, basename)) {
        manifest_ok = 0;
        break;
      }
    }
  }
  if (manifest_ok) {
    voco_eval_frame(w, frame.data);
  } else {
    reject_loaded_project();
    project_file_error(w, "Not enough memory to load the project.");
  }
  free(frame.data);
  free(json);
}

static void accept_loaded_project(void) {
  if (!pending_project_temp_directory[0]) return;
  cleanup_project_directory(project_temp_directory);
  snprintf(
    project_temp_directory, sizeof(project_temp_directory),
    "%s", pending_project_temp_directory);
  pending_project_temp_directory[0] = '\0';
}

static void reject_loaded_project(void) {
  cleanup_project_directory(pending_project_temp_directory);
  pending_project_temp_directory[0] = '\0';
}

static void load_project_dialog(struct webview *w) {
  char filename[PATH_MAX] = "";
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE,
    "Load project", "", filename, sizeof(filename));
  if (filename[0]) load_project_file(w, filename);
}

static void handle_project_command(struct webview *w, const char *arg) {
  switch (arg[0]) {
    case 'B': begin_project_save(w); break;
    case 'W': add_project_wave(w, &arg[1]); break;
    case 'X': add_project_file(w, &arg[1]); break;
    case 'F': finish_project_save(w, &arg[1]); break;
    case 'L': load_project_dialog(w); break;
    case 'A': accept_loaded_project(); break;
    case 'R': reject_loaded_project(); break;
    default: break;
  }
}

static void choose_project_file(struct webview *w, const char *arg) {
  char *end;
  long index = strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || index < -1 || index >= PROJECT_MAX_FILES) {
    return;
  }

  char filename[PATH_MAX] = "";
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE,
    "Add project file", "", filename, sizeof(filename));
  if (!filename[0]) return;

  char index_text[32];
  snprintf(index_text, sizeof(index_text), "%ld", index);
  const char *payloads[] = {
    index_text, filename, path_basename(filename)
  };
  voco_send_fields(w, "ui", "managedFileChosen", "A", payloads, 3);
}

static void send_audio_devices(struct webview *w) {
  char *log = skoder("/als");
  const char *status;
  addSkodeLog(w, log);
  voco_send(w, "ui", "clearAudioDevices", "A", "");

  for (int is_capture = 0; is_capture <= 1; is_capture++) {
    const char *kind = is_capture ? "input" : "output";
    for (int i = 0; i < skred_devices(is_capture); i++) {
      char selection[32];
      snprintf(selection, sizeof(selection), "%d", i);
      const char *payloads[] = {
        kind, selection, skred_device_str(is_capture, i)
      };
      voco_send_fields(w, "ui", "addAudioDevice", "A", payloads, 3);
    }
  }

  status = skred_audio_status();
  voco_send(w, "ui", "audioDevicesReady", "A", status ? status : "");
}

static int parse_audio_device(
    const char *arg, int *is_capture, int *selection) {
  char *end;
  long capture = strtol(arg, &end, 10);
  if (end == arg || *end != ':' || (capture != 0 && capture != 1)) return 0;

  const char *selection_arg = end + 1;
  long selected = strtol(selection_arg, &end, 10);
  if (end == selection_arg || *end != '\0' ||
      selected < -2 || selected > INT_MAX) return 0;

  *is_capture = (int)capture;
  *selection = (int)selected;
  return 1;
}

static void notify_audio_device_applied(
    struct webview *w, int is_capture, int success, const char *status) {
  const char *payloads[] = {
    is_capture ? "input" : "output",
    success ? "true" : "false",
    status ? status : ""
  };
  voco_send_fields(w, "ui", "audioDeviceApplied", "A", payloads, 3);
}

static void apply_audio_device(struct webview *w, const char *arg) {
  int is_capture;
  int selection;
  if (!parse_audio_device(arg, &is_capture, &selection)) return;

  int result = skred_audio_select(is_capture, selection);
  if (result == 0) result = skred_audio_reconnect();
  notify_audio_device_applied(
    w, is_capture, result == 0, skred_audio_status());
}

static void choose_wave_directory(struct webview *w) {
  char dirname[PATH_MAX] = "";
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_DIRECTORY,
    "sel", "", dirname, sizeof(dirname));
  if (dirname[0]) load_wave_directory(w, dirname);
}

static void resize_main_window(struct webview *w, const char *arg) {
  char *end;
  long width = strtol(arg, &end, 10);
  if (end == arg || *end != ':' || width < 320 || width > 10000) return;

  const char *height_arg = end + 1;
  long height = strtol(height_arg, &end, 10);
  if (end == height_arg || *end != '\0' ||
      height < 240 || height > 10000) {
    return;
  }

#if defined(WEBVIEW_GTK)
  gtk_window_resize(GTK_WINDOW(w->priv.window), (int)width, (int)height);
#elif defined(WEBVIEW_COCOA)
  CGSize size = CGSizeMake((CGFloat)width, (CGFloat)height);
  ((void(*)(id, SEL, CGSize))objc_msgSend)(
    w->priv.window, sel_registerName("setContentSize:"), size);
#elif defined(WEBVIEW_WINAPI)
  RECT rect = {0, 0, (LONG)width, (LONG)height};
  DWORD style = (DWORD)GetWindowLongPtr(w->priv.hwnd, GWL_STYLE);
  DWORD ex_style = (DWORD)GetWindowLongPtr(w->priv.hwnd, GWL_EXSTYLE);
  if (!AdjustWindowRectEx(&rect, style, FALSE, ex_style)) return;
  SetWindowPos(
    w->priv.hwnd, NULL, 0, 0,
    rect.right - rect.left, rect.bottom - rect.top,
    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

static void handle_audio_device_command(struct webview *w, const char *arg) {
  if (arg[0] == 'R') {
    send_audio_devices(w);
  } else if (arg[0] == 'A') {
    apply_audio_device(w, &arg[1]);
  }
}

static void handle_visual_scope_command(struct webview *w, const char *arg) {
  if (arg[0] == 'S') {
    if (ensure_visual_scope(w)) {
      send_visual_scope_status(w, "Scope running.", 1);
      poll_visual_scope(w);
    }
  } else if (arg[0] == 'P') {
    poll_visual_scope(w);
  } else if (arg[0] == 'T') {
    stop_visual_scope();
    send_visual_scope_status(w, "Scope stopped.", 0);
  }
}

static void load_settings_dialog(struct webview *w) {
  char filename[PATH_MAX] = "";
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE,
    "Load settings", "", filename, sizeof(filename));
  if (filename[0]) load_settings_file(w, filename);
}

static void handle_settings_command(struct webview *w, const char *arg) {
  if (arg[0] == 'S') {
    save_settings_file(w, &arg[1]);
  } else if (arg[0] == 'L') {
    load_settings_dialog(w);
  }
}

static void handle_load_wave_command(struct webview *w, const char *arg) {
  char *end;
  long voice = strtol(arg, &end, 10);
  if (end != arg && *end == ':' && voice >= 0 && voice + 1 < 32) {
    load_wave_file(w, end + 1, (int)voice);
  }
}

static void choose_wave_file(struct webview *w, const char *arg) {
  if (arg[0] != 'v') return;

  char filename[PATH_MAX] = "";
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE,
    "sel", "", filename, sizeof(filename));

  char *end;
  long voice = strtol(&arg[1], &end, 10);
  if (filename[0] && end != &arg[1] && *end == '\0' &&
      voice >= 0 && voice + 1 < 32) {
    load_wave_file(w, filename, (int)voice);
  }
}

static int voco_native_argc(const struct voco_message *message) {
  return message->count - 3;
}

static const struct voco_field *voco_native_arg(
    const struct voco_message *message, int index) {
  if (index < 0 || index >= voco_native_argc(message)) return NULL;
  return &message->fields[index + 3];
}

static void handle_voco_native(struct webview *w, const struct voco_message *m) {
  const struct voco_field *command;
  long value;
  char *text;
  (void)w;

  if (m->count < 3 ||
      !voco_field_equals(&m->fields[0], "native")) {
    return;
  }
  command = &m->fields[1];

  if (voco_field_equals(command, "engine") && voco_native_argc(m) >= 1) {
    text = voco_field_cstr(voco_native_arg(m, 0));
    if (text) {
      addSkodeLog(w, skoder(text));
      free(text);
    }
  } else if (voco_field_equals(command, "chooseDirectory")) {
    choose_wave_directory(w);
  } else if (voco_field_equals(command, "chooseWave") &&
      voco_field_long(voco_native_arg(m, 0), 0, 30, &value)) {
    char voice[32];
    snprintf(voice, sizeof(voice), "v%ld", value);
    choose_wave_file(w, voice);
  } else if (voco_field_equals(command, "loadDirectory") &&
      voco_native_argc(m) >= 1) {
    text = voco_field_cstr(voco_native_arg(m, 0));
    if (text) {
      load_wave_directory(w, text);
      free(text);
    }
  } else if (voco_field_equals(command, "loadWave") &&
      voco_field_long(voco_native_arg(m, 0), 0, 30, &value) &&
      voco_native_argc(m) >= 2) {
    text = voco_field_cstr(voco_native_arg(m, 1));
    if (text) {
      load_wave_file(w, text, (int)value);
      free(text);
    }
  } else if (voco_field_equals(command, "chooseManagedFile") &&
      voco_field_long(voco_native_arg(m, 0), -1, PROJECT_MAX_FILES - 1, &value)) {
    char index[32];
    snprintf(index, sizeof(index), "%ld", value);
    choose_project_file(w, index);
  } else if (voco_field_equals(command, "saveSettings") &&
      voco_native_argc(m) >= 1) {
    text = voco_field_cstr(voco_native_arg(m, 0));
    if (text) {
      save_settings_file(w, text);
      free(text);
    }
  } else if (voco_field_equals(command, "loadSettings")) {
    load_settings_dialog(w);
  } else if (voco_field_equals(command, "beginProjectSave")) {
    begin_project_save(w);
  } else if ((voco_field_equals(command, "addProjectWave") ||
        voco_field_equals(command, "addProjectFile")) &&
      voco_native_argc(m) >= 3) {
    char *archive_name = voco_field_cstr(voco_native_arg(m, 1));
    char *filename = voco_field_cstr(voco_native_arg(m, 2));
    if (voco_field_equals(command, "addProjectWave") &&
        voco_field_long(voco_native_arg(m, 0), 0, PROJECT_MAX_WAVES - 1,
            &value) &&
        archive_name && filename) {
      add_project_wave_values(w, archive_name, filename);
    } else if (voco_field_equals(command, "addProjectFile") &&
        voco_field_long(voco_native_arg(m, 0), 0, PROJECT_MAX_FILES - 1,
            &value) &&
        archive_name && filename) {
      add_project_file_values(w, archive_name, filename);
    }
    (void)value;
    free(archive_name);
    free(filename);
  } else if (voco_field_equals(command, "finishProjectSave") &&
      voco_native_argc(m) >= 1) {
    text = voco_field_cstr(voco_native_arg(m, 0));
    if (text) {
      finish_project_save(w, text);
      free(text);
    }
  } else if (voco_field_equals(command, "loadProject")) {
    load_project_dialog(w);
  } else if (voco_field_equals(command, "acceptProject")) {
    accept_loaded_project();
  } else if (voco_field_equals(command, "rejectProject")) {
    reject_loaded_project();
  } else if (voco_field_equals(command, "resizeMainWindow") &&
      voco_native_argc(m) >= 2) {
    long height;
    if (voco_field_long(voco_native_arg(m, 0), 320, 10000, &value) &&
        voco_field_long(voco_native_arg(m, 1), 240, 10000, &height)) {
      char geometry[64];
      snprintf(geometry, sizeof(geometry), "%ld:%ld", value, height);
      resize_main_window(w, geometry);
    }
  } else if (voco_field_equals(command, "startVisualScope")) {
    if (ensure_visual_scope(w)) {
      send_visual_scope_status(w, "Scope running.", 1);
      poll_visual_scope(w);
    }
  } else if (voco_field_equals(command, "pollVisualScope")) {
    poll_visual_scope(w);
  } else if (voco_field_equals(command, "stopVisualScope")) {
    stop_visual_scope();
    send_visual_scope_status(w, "Scope stopped.", 0);
  } else if (voco_field_equals(command, "refreshAudioDevices")) {
    send_audio_devices(w);
  } else if (voco_field_equals(command, "applyAudioDevice") &&
      voco_native_argc(m) >= 2) {
    long selection;
    if (voco_field_long(voco_native_arg(m, 0), 0, 1, &value) &&
        voco_field_long(voco_native_arg(m, 1), -2, INT_MAX, &selection)) {
      char device[64];
      snprintf(device, sizeof(device), "%ld:%ld", value, selection);
      apply_audio_device(w, device);
    }
  }
}

static void invoker(struct webview *w, const char *arg) {
  struct voco_message message;
  if (!arg || !arg[0]) return;

  if (voco_parse(arg, &message)) {
    handle_voco_native(w, &message);
    return;
  }

  switch (arg[0]) {
    case NATIVE_ENGINE_COMMAND:
      addSkodeLog(w, skoder(&arg[1]));
      break;
    case NATIVE_CHOOSE_DIRECTORY:
      choose_wave_directory(w);
      break;
    case NATIVE_AUDIO_DEVICE:
      handle_audio_device_command(w, &arg[1]);
      break;
    case NATIVE_SETTINGS:
      handle_settings_command(w, &arg[1]);
      break;
    case NATIVE_PROJECT:
      handle_project_command(w, &arg[1]);
      break;
    case NATIVE_WINDOW_GEOMETRY:
      resize_main_window(w, &arg[1]);
      break;
    case NATIVE_VISUAL_SCOPE:
      handle_visual_scope_command(w, &arg[1]);
      break;
    case NATIVE_CHOOSE_PROJECT_FILE:
      choose_project_file(w, &arg[1]);
      break;
    case NATIVE_LOAD_DIRECTORY:
      load_wave_directory(w, &arg[1]);
      break;
    case NATIVE_LOAD_WAVE:
      handle_load_wave_command(w, &arg[1]);
      break;
    case NATIVE_CHOOSE_WAVE:
      choose_wave_file(w, &arg[1]);
      break;
    default:
      break;
  }
}

static int confirm_close(struct webview *w) {
  char result[2];
  webview_dialog(
    w, WEBVIEW_DIALOG_TYPE_CONFIRM, WEBVIEW_DIALOG_FLAG_WARNING,
    "Quit ro-totem?", "Are you sure you want to quit?", result, sizeof(result));
  return result[0] == '1';
}

static void set_build_versions(struct webview *w) {
  const char *payloads[] = {ROTOTEM_VERSION, skred_version()};
  voco_send_fields(w, "ui", "setBuildVersions", "A", payloads, 2);
}

int main(void) {
#ifdef __APPLE__
  char html_path[PATH_MAX * 3 + 8];
  char tmp[PATH_MAX];

  if (!get_resource_path("ui.html", tmp, sizeof(tmp)) ||
      !make_file_url(tmp, html_path, sizeof(html_path))) {
    fputs("Could not locate ui.html\n", stderr);
    return 1;
  }
#else
  char *html_path = make_embedded_ui_url();
  if (!html_path) {
    fputs("Could not load embedded ui.html\n", stderr);
    return 1;
  }
#endif

  struct webview webview;
  char title[128];
  snprintf(title, sizeof(title), "ro-totem %s", ROTOTEM_VERSION);
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = title;
  webview.width = 884;  // window.innerWidth
#ifdef __linux__
  webview.height = 740;
#else
  webview.height = 700;
#endif
  webview.resizable = 1;
  webview.debug = 1;
  webview.external_invoke_cb = &invoker;
  webview.close_cb = &confirm_close;

  skred_enumerate_devices(0);
  skred_enumerate_devices(1);
  int output = 0;
  int input = 0;
  int req = 128;
  int vc = 32;
#ifdef __APPLE__
  // grab MacBook Pro Speakers
  for (int i=0; i<skred_devices(0); i++) {
    if (strcmp("MacBook Pro Speakers", skred_device_str(0, i)) == 0) {
      output = skred_device_idx(0, i);
      break;
    }
  }
  // grab MacBook Pro Microphone
  for (int i=0; i<skred_devices(1); i++) {
    if (strcmp("MacBook Pro Microphone", skred_device_str(1, i)) == 0) {
      input = skred_device_idx(1, i);
      break;
    }
  }
#endif
  skred_set_audio_device(output, input);
  skred_start(req, vc, -1);
  skred_logger(1);

  int r = webview_init(&webview);
  if (r != 0) {
    fputs("Could not initialize the webview\n", stderr);
    skred_stop();
    return 1;
  }

  set_build_versions(&webview);
  skoder("S100v0a0f440>1>2>3>4>5>6>7>8>9>10>11>12>13>14>15");

  do {
    r = webview_loop(&webview, 1);
  } while (r == 0);

  stop_visual_scope();
  skred_stop();
  webview_exit(&webview);
  discard_project_writer(1);
  cleanup_project_directory(project_temp_directory);
  cleanup_project_directory(pending_project_temp_directory);
#ifndef __APPLE__
  free(html_path);
#endif

  return 0;
}
