#include "vendor/skred/include/api.h"

#define WEBVIEW_IMPLEMENTATION
/* Define WEBVIEW_WINAPI, WEBVIEW_GTK, or WEBVIEW_COCOA when compiling. */
#include "vendor/webview/webview.h"
#include "vendor/miniz/miniz.h"

#include <limits.h>
#include <stdarg.h>
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

static void addLog(struct webview *w, const char *message);

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
    skred_command(msg);
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
 * Messages from ui.html use a small envelope protocol:
 *
 *   !<commands>       Pass compact commands to the audio engine.
 *   @                 Open the wave-directory chooser.
 *   >v<voice>         Choose a wave for a stereo voice pair.
 *   R<directory>      Scan a directory and return its .wav filenames.
 *   W<voice>:<path>   Load a wave into voice and voice + 1.
 *   JS<json> / JL     Save or load settings.
 *   PB / PW / PF / PL Save or load a project ZIP containing settings and WAVs.
 *   DR / DA<c>:<n>    Refresh or apply an audio-device selection.
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
  NATIVE_LOAD_DIRECTORY = 'R',
  NATIVE_LOAD_WAVE = 'W',
  NATIVE_CHOOSE_WAVE = '>'
};

static void load_wave_directory(struct webview *w, const char *dirname) {
  webview_eval(w, "clearWaveFiles()");
  struct script_builder script = {0};
  if (script_append(&script, "setWaveDirectory(") &&
      script_append_js_string(&script, dirname) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }

  DIR *dp = opendir(dirname);
  if (!dp) return;

  struct dirent *entry;
  while ((entry = readdir(dp))) {
    const char *name = entry->d_name;
    size_t len = strlen(name);
    if (len <= 4 || strcasecmp(name + len - 4, ".wav") != 0) continue;

    script = (struct script_builder){0};
    if (script_append(&script, "addWaveFile(") &&
        script_append_js_string(&script, name) &&
        script_append(&script, ")")) {
      script_eval(w, &script);
    } else {
      free(script.data);
    }
  }
  closedir(dp);
}

static void addLog(struct webview *w, const char *message) {
  struct script_builder script = {0};
  if (script_append(&script, "addLog(") &&
      script_append_js_string(&script, message) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
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
  struct script_builder script = {0};
  if (script_appendf(&script, "setTrackWave(%d,", voice) &&
      script_append_js_string(&script, filename) &&
      script_append(&script, ",") &&
      script_append_js_string(&script, shortname) &&
      script_appendf(&script, ",%d,%d)", waves[0], waves[1])) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
}

static void apply_loaded_wave_controls(struct webview *w, int voice) {
  struct script_builder script = {0};
  if (script_appendf(&script, "applyTrackControls(%d)", voice)) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
  webview_eval(w, "waveFileLoaded()");
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
    webview_eval(w, "settingsFileError('Could not open settings file.')");
    return;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    webview_eval(w, "settingsFileError('Could not read settings file.')");
    return;
  }

  long length = ftell(file);
  if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    webview_eval(w, "settingsFileError('Settings file is too large or invalid.')");
    return;
  }

  char *json = malloc((size_t)length + 1);
  if (!json) {
    free(json);
    fclose(file);
    webview_eval(w, "settingsFileError('Not enough memory to load settings.')");
    return;
  }

  size_t read = fread(json, 1, (size_t)length, file);
  int read_error = ferror(file);
  fclose(file);
  if (read != (size_t)length || read_error) {
    free(json);
    webview_eval(w, "settingsFileError('Could not read settings file.')");
    return;
  }
  json[read] = '\0';

  struct script_builder script = {0};
  if (script_append(&script, "loadSettingsFromText(") &&
      script_append_js_string(&script, json) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
    webview_eval(w, "settingsFileError('Not enough memory to load settings.')");
  }
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
      webview_eval(w, "settingsFileError('Settings filename is too long.')");
      return;
    }
    memcpy(filename + length, ".json", 6);
  }

  FILE *file = fopen(filename, "wb");
  if (!file) {
    webview_eval(w, "settingsFileError('Could not create settings file.')");
    return;
  }

  size_t json_length = strlen(json);
  int ok = fwrite(json, 1, json_length, file) == json_length;
  ok = fclose(file) == 0 && ok;
  webview_eval(w, ok
    ? "settingsFileSaved()"
    : "settingsFileError('Could not write settings file.')");
}

#define PROJECT_MAX_WAVES 32
#define PROJECT_MAX_SETTINGS_SIZE (1024 * 1024)
#define PROJECT_MAX_WAVE_SIZE ((mz_uint64)1024 * 1024 * 1024)
#define PROJECT_MAX_TOTAL_WAVE_SIZE ((mz_uint64)4 * 1024 * 1024 * 1024)

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
  struct script_builder script = {0};
  if (script_append(&script, "settingsFileError(") &&
      script_append_js_string(&script, message) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
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
    webview_eval(w, "projectFileCancelled()");
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
  webview_eval(w, "projectSaveReady()");
}

static int parse_project_wave_argument(
    const char *arg, int *index, char *archive_name,
    size_t archive_name_size, const char **filename) {
  char *end;
  long parsed = strtol(arg, &end, 10);
  if (end == arg || *end != ':' ||
      parsed < 0 || parsed >= PROJECT_MAX_WAVES) {
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

static void add_project_wave(struct webview *w, const char *arg) {
  int index;
  char archive_name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  const char *filename;
  if (!project_writer.active || project_writer.failed) return;
  if (!parse_project_wave_argument(
        arg, &index, archive_name, sizeof(archive_name), &filename) ||
      !valid_project_wave_name(archive_name)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not save a WAV with an invalid path.");
    return;
  }
  (void)index;

  if (!mz_zip_writer_add_file(
        &project_writer.archive, archive_name, filename,
        NULL, 0, MZ_NO_COMPRESSION)) {
    project_writer.failed = 1;
    project_file_error(w, "Could not add a WAV file to the project archive.");
  }
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
  struct script_builder script = {0};
  if (script_append(&script, "projectFileSaved(") &&
      script_append_js_string(&script, project_writer.filename) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
    webview_eval(w, "projectFileSaved('')");
  }
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

  char waves_directory[PATH_MAX];
  if (!join_path(
        waves_directory, sizeof(waves_directory), directory, "waves")) {
    return;
  }

  DIR *dp = opendir(waves_directory);
  if (dp) {
    struct dirent *entry;
    while ((entry = readdir(dp))) {
      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      char filename[PATH_MAX];
      if (join_path(
            filename, sizeof(filename), waves_directory, entry->d_name)) {
        remove(filename);
      }
    }
    closedir(dp);
  }
  remove_directory(waves_directory);
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
};

static int inspect_project_archive(
    mz_zip_archive *archive, struct project_archive_contents *contents) {
  mz_uint file_count = mz_zip_reader_get_num_files(archive);
  if (file_count == 0 || file_count > PROJECT_MAX_WAVES + 1) return 0;

  memset(contents, 0, sizeof(*contents));
  contents->settings_index = -1;

  mz_uint64 total_wave_size = 0;
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

    if (!valid_project_wave_name(stat.m_filename) ||
        contents->wave_count >= PROJECT_MAX_WAVES ||
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
    ok = extract_project_waves(&archive, &contents, temp_directory);
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

  struct script_builder script = {0};
  if (script_append(&script, "loadProjectFromText(") &&
      script_append_js_string(&script, json) &&
      script_append(&script, ",") &&
      script_append_js_string(&script, pending_project_temp_directory) &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
    reject_loaded_project();
    project_file_error(w, "Not enough memory to load the project.");
  }
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
    case 'F': finish_project_save(w, &arg[1]); break;
    case 'L': load_project_dialog(w); break;
    case 'A': accept_loaded_project(); break;
    case 'R': reject_loaded_project(); break;
    default: break;
  }
}

static void send_audio_devices(struct webview *w) {
  char *log = skoder("/als");
  const char *status;
  addSkodeLog(w, log);
  webview_eval(w, "clearAudioDevices()");

  for (int is_capture = 0; is_capture <= 1; is_capture++) {
    const char *kind = is_capture ? "input" : "output";
    for (int i = 0; i < skred_devices(is_capture); i++) {
      struct script_builder script = {0};
      if (script_appendf(&script, "addAudioDevice('%s',%d,", kind, i) &&
          script_append_js_string(&script, skred_device_str(is_capture, i)) &&
          script_append(&script, ")")) {
        script_eval(w, &script);
      } else {
        free(script.data);
      }
    }
  }

  status = skred_audio_status();
  struct script_builder script = {0};
  if (script_append(&script, "audioDevicesReady(") &&
      script_append_js_string(&script, status ? status : "") &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
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
  struct script_builder script = {0};
  if (script_appendf(
        &script,
        "audioDeviceApplied('%s',%s,",
        is_capture ? "input" : "output",
        success ? "true" : "false") &&
      script_append_js_string(&script, status ? status : "") &&
      script_append(&script, ")")) {
    script_eval(w, &script);
  } else {
    free(script.data);
  }
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

static void handle_audio_device_command(struct webview *w, const char *arg) {
  if (arg[0] == 'R') {
    send_audio_devices(w);
  } else if (arg[0] == 'A') {
    apply_audio_device(w, &arg[1]);
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

static void invoker(struct webview *w, const char *arg) {
  if (!arg || !arg[0]) return;

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

int main(void) {
  char html_path[PATH_MAX * 3 + 8];
  char tmp[PATH_MAX];

  if (!get_resource_path("ui.html", tmp, sizeof(tmp)) ||
      !make_file_url(tmp, html_path, sizeof(html_path))) {
    fputs("Could not locate ui.html\n", stderr);
    return 1;
  }

  struct webview webview;
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = "ro-totem gemini epsilon-four 2026";
  webview.width = 884;  // window.innerWidth
#ifdef __linux__
  webview.height = 740;
#else
  webview.height = 700;
#endif
  webview.resizable = 1;
  webview.debug = 1;
  webview.external_invoke_cb = &invoker;

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
    skoder("/q");
    sleep(1);
    return 1;
  }

  skoder("S100v0a0f440>1>2>3>4>5>6>7>8>9>10>11>12>13>14>15");

  do {
    r = webview_loop(&webview, 1);
  } while (r == 0);

  webview_exit(&webview);
  discard_project_writer(1);
  cleanup_project_directory(project_temp_directory);
  cleanup_project_directory(pending_project_temp_directory);

  // tell it to quit...
  skoder("/q");

  sleep(1);

  return 0;
}
