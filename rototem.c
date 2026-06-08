#include "api.h"

#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
void get_resource_path(const char *filename, char *out_path) {
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
  char path[PATH_MAX];
    
  if (CFURLGetFileSystemRepresentation(resourcesURL, true, (UInt8 *)path, PATH_MAX)) {
    snprintf(out_path, PATH_MAX, "%s/%s", path, filename);
  }

  CFRelease(resourcesURL);
}
#else
#include <sys/wait.h>
void get_resource_path(const char *filename, char *out_path) {
  char path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path)-1);
  if (len >= 0) path[len] = '\0';
  char *last = strrchr(path, '/');
  if (last) *last = '\0';
  sprintf(out_path, "%s/%s", path, filename);
}
#endif

void get_bundle_resource_path(const char *filename, char *out_path, int max_len) {
#ifdef __APPLE__
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFURLRef resURL = CFBundleCopyResourceURL(mainBundle, 
    CFStringCreateWithCString(NULL, filename, kCFStringEncodingUTF8), NULL, NULL);
  if (!resURL) return;
  CFURLGetFileSystemRepresentation(resURL, true, (UInt8 *)out_path, max_len);
  CFRelease(resURL);
#else
  strcpy(out_path, filename);
#endif
}

void addLog(struct webview *w, char *out);

int handle_audio_command(const char *line) {
  int result = skred_audio_command(line);
  if (result == 0) return 0;
  const char *message = skred_audio_message();
  if (message && message[0]) printf("%s\n", message);
  return 1;
}

char *skoder(const char *msg, char flag) {
  char *log = "";
  if (handle_audio_command(msg)) {
    log = skred_audio_message();
  } else {
    skred_command(msg);
    log = skred_log();
  }
  return log;
}

void addSkodeLog(struct webview *w, char *log) {
  if (log && log[0] != '\0') {
    char *end;
    char *s = log;
    while ((end = strchr(s, '\n'))) {
      *end = '\0'; // Terminate at newline
      // Pointer math: check one byte back for '\r' and strip it
      if (end > s && end[-1] == '\r') end[-1] = '\0';
      //printf("Line: %s\n", s); // Pass 's' to your evaluator here
      addLog(w, s);
      s = end + 1; // Advance pointer past the newline
    }
    // Handle the final segment if it lacks a trailing newline
    if (*s) {
      //printf("Line: %s\n", s);
      addLog(w, s);
    }
  }
}

static int wavepointer = 300;

static char *append_js_string(char *out, const char *value) {
  *out++ = '\'';
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    switch (*p) {
      case '\\': *out++ = '\\'; *out++ = '\\'; break;
      case '\'': *out++ = '\\'; *out++ = '\''; break;
      case '\n': *out++ = '\\'; *out++ = 'n'; break;
      case '\r': *out++ = '\\'; *out++ = 'r'; break;
      case '\t': *out++ = '\\'; *out++ = 't'; break;
      default: *out++ = (char)*p; break;
    }
  }
  *out++ = '\'';
  return out;
}

void addLog(struct webview *w, char *message) {
  char script[PATH_MAX * 2 + 256];
  char *out = script;
  out += sprintf(out, "addLog(");
  out = append_js_string(out, message);
  *out++ = ')';
  *out = '\0';
  webview_eval(w, script);
}

static void load_wave_file(struct webview *w, const char *filename, int voice) {
  if (!filename[0] || voice < 0 || voice + 1 >= 32) return;

  char cmd[PATH_MAX + 128];
  char *log;
  const char *shortname = filename;
  for (const char *p = filename; *p; p++) {
    if (*p == '/' || *p == '\\') shortname = p + 1;
  }

  for (int j = 0; j < 2; j++) {
    int wave = wavepointer++;
    if (wavepointer > 998) wavepointer = 0;
    int pan = (j & 1) ? 1 : -1;
    snprintf(cmd, sizeof(cmd),
      "[%s] /ws%d %d v%d p%d w%d a0 B1 f440 t1 0 1 1",
      filename, wave, j, voice + j, pan, wave);
    addLog(w, cmd);
    log = skoder(cmd, 0);
    addSkodeLog(w, log);
    snprintf(cmd, sizeof(cmd), "[%s] wt %d", shortname, wave);
    log = skoder(cmd, 0);
    addSkodeLog(w, log);
  }

  char script[PATH_MAX * 2 + 64];
  char *out = script;
  out += sprintf(out, "setTrackWave(%d,", voice);
  out = append_js_string(out, filename);
  *out++ = ',';
  out = append_js_string(out, shortname);
  *out++ = ')';
  *out = '\0';
  webview_eval(w, script);
  webview_eval(w, "waveFileLoaded()");
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
  char *script = malloc((size_t)length * 6 + 32);
  if (!json || !script) {
    free(json);
    free(script);
    fclose(file);
    webview_eval(w, "settingsFileError('Not enough memory to load settings.')");
    return;
  }

  size_t read = fread(json, 1, (size_t)length, file);
  fclose(file);
  json[read] = '\0';

  char *out = script;
  out += sprintf(out, "loadSettingsFromText(\"");
  for (size_t i = 0; i < read; i++) {
    unsigned char c = (unsigned char)json[i];
    switch (c) {
      case '\\': *out++ = '\\'; *out++ = '\\'; break;
      case '"': *out++ = '\\'; *out++ = '"'; break;
      case '\n': *out++ = '\\'; *out++ = 'n'; break;
      case '\r': *out++ = '\\'; *out++ = 'r'; break;
      case '\t': *out++ = '\\'; *out++ = 't'; break;
      default:
        if (c < 0x20) {
          out += sprintf(out, "\\u%04x", c);
        } else {
          *out++ = (char)c;
        }
    }
  }
  strcpy(out, "\")");
  webview_eval(w, script);
  free(script);
  free(json);
}

static void save_settings_file(struct webview *w, const char *json) {
  char filename[PATH_MAX];
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
    strcat(filename, ".json");
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

static void send_audio_devices(struct webview *w) {
  char script[PATH_MAX * 2 + 128];
  char *log = skoder("/als", 0);
  addSkodeLog(w, log);
  webview_eval(w, "clearAudioDevices()");

  for (int is_capture = 0; is_capture <= 1; is_capture++) {
    const char *kind = is_capture ? "input" : "output";
    for (int i = 0; i < skred_devices(is_capture); i++) {
      char *out = script;
      out += sprintf(out, "addAudioDevice('%s',%d,", kind, i);
      out = append_js_string(out, skred_device_str(is_capture, i));
      *out++ = ')';
      *out = '\0';
      webview_eval(w, script);
    }
  }

  webview_eval(w, "audioDevicesReady()");
}

static void invoker(struct webview *w, const char *arg) {
  char cmd[1024];
  char *log;
  switch (arg[0]) {
    case '!':
      log = skoder(&arg[1], 0);
      addSkodeLog(w, log);
      break;
    case '@': // allow the user to select a folder, stuff dir name and wav file list into the webview
      {
        char dirname[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_DIRECTORY, "sel", "", dirname, sizeof(dirname));
        // stuff the dir name into 'dir'
        sprintf(cmd, "vassign('dir','%s');", dirname);
        webview_eval(w, cmd);
        webview_eval(w, "lclear()"); // this clears the 'file' array
        struct dirent *entry;
        DIR *dp = opendir(dirname);
        if (dp) {
          char cmd[1024];
          while ((entry = readdir(dp))) {
            char *name = entry->d_name;
            size_t len = strlen(name);
            if ((len > 4) && (strcasecmp(name + len - 4, ".wav") == 0)) {
              sprintf(cmd, "lstuff('%s')", name); // this pushes the name into the 'file' array
              webview_eval(w, cmd);
            }
          }
        }
      }
      break;
    case 'D':
      if (arg[1] == 'R') send_audio_devices(w);
      break;
    case 'J':
      if (arg[1] == 'S') {
        save_settings_file(w, &arg[2]);
      } else if (arg[1] == 'L') {
        char filename[PATH_MAX];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE,
                       "Load settings", "", filename, sizeof(filename));
        if (filename[0]) load_settings_file(w, filename);
      }
      break;
    case 'W':
      {
        char *end;
        long voice = strtol(&arg[1], &end, 10);
        if (end && *end == ':' && voice >= 0 && voice + 1 < 32) {
          load_wave_file(w, end + 1, (int)voice);
        }
      }
      break;
    case '>': // tell skred to read the filename into a voice (via 'filename'), setup the voice
      // pick in the ui
      if (arg[1] == 'v') {
        char filename[PATH_MAX] = "";
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE, "sel", "", filename, sizeof(filename));
        int voice = atoi(&arg[2]);
        load_wave_file(w, filename, voice);
      }
      break;
    default:
      break;
  }
}

void info(struct webview *w) {
  static int run = 0;
  char buf[1024];
  
  sprintf(buf, "# run %d", run++);
  puts(buf);
  addLog(w, buf);
  strcpy(buf, "# output devices");
  puts(buf);
  addLog(w, buf);
  for (int i=0; i<skred_devices(0); i++) {
    sprintf(buf, "# %d %s", skred_device_idx(0, i), skred_device_str(0, i));
    puts(buf);
    addLog(w, buf);
  }
  
  strcpy(buf, "# input devices");
  puts(buf);
  addLog(w, buf);
  for (int i=0; i<skred_devices(1); i++) {
    sprintf(buf, "# %d %s", skred_device_idx(1, i), skred_device_str(1, i));
    puts(buf);
    addLog(w, buf);
  }
}

#define FILE_URL "file://"

int main(int argc, char *argv[]) {
  char html_path[PATH_MAX];
  char bin_path[PATH_MAX];
  char tmp[PATH_MAX];

  get_resource_path("ui.html", tmp);
  sprintf(html_path, "file://%s", tmp);
  //printf("html_path {%s}\n", html_path);

  struct webview webview;
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = "ro-totem gemini gamma 2026";
  webview.width = 884;  // window.innerWidth
  webview.height = 700; // window.innerHeight
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
      //printf("# USED [%d]/%d for output\n", i, output);
      break;
    }
  }
  // grab MacBook Pro Microphone
  for (int i=0; i<skred_devices(1); i++) {
    if (strcmp("MacBook Pro Microphone", skred_device_str(1, i)) == 0) {
      input = skred_device_idx(1, i);
      //printf("# USED [%d]/%d for input\n", i, input);
      break;
    }
  }
#endif
  skred_set_audio_device(output, input);
  skred_start(req, vc, -1);
  skred_logger(1);
  
  int r = webview_init(&webview);
  
  skoder("S100v0a0f440>1>2>3>4>5>6>7>8>9>10>11>12>13>14>15", 0);

#if 0
  int first = 20;
#endif

  do {
    r = webview_loop(&webview, 1);
#if 0
    if (first > 0) {
      first--;
      info(&webview);
    }
#endif
  } while (r == 0);

  // tell it to quit...
  skoder("/q", 0);
  
  sleep(1);
  
  return 0;
}
