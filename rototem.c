#include "api.h"

#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
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

char *skoder(const char *msg, char flag) {
  int r = skred_command(msg);
  char *log = skred_log();
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
      printf("Line: %s\n", s); // Pass 's' to your evaluator here
      addLog(w, s);
      s = end + 1; // Advance pointer past the newline
    }
    // Handle the final segment if it lacks a trailing newline
    if (*s) {
      printf("Line: %s\n", s);
      addLog(w, s);
    }
  }
}

static int wavepointer = 300;

void addLog(struct webview *w, char *out) {
  char res[4096];
  sprintf(res, "addLog('%s')", out);
  webview_eval(w, res);
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
        sprintf(cmd, "assign('dir','%s');", dirname);
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
    case '>': // tell skred to read the filename into a voice (via 'filename'), setup the voice
      if (arg[1] == 'v') {
        char filename[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE, "sel", "", filename, sizeof(filename));
        int voice = arg[2] - '0';
        sprintf(cmd, "[%s] /ws%d v%d w%d a0 B1 f440 t1 0 1 1", filename, wavepointer, voice, wavepointer);
        addLog(w, cmd);
        wavepointer++;
        if (wavepointer > 999) wavepointer = 0;
        log = skoder(cmd, 0);
        addSkodeLog(w, log);
        int len = strlen(filename);
        char *ptr = filename;
        for (int i=len; i>0; i--) {
          if (ptr[i-1] == '/') {
            ptr += i;
            break;
          }
        }
        sprintf(cmd, "assign('v%d','%s');", voice, ptr);
        webview_eval(w, cmd);
        sprintf(cmd, "[%s] wt %d", ptr, wavepointer);
        printf("name it : %s\n", cmd);
        log = skoder(cmd, 0);
        addSkodeLog(w, log);
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
  printf("html_path {%s}\n", html_path);

  struct webview webview;
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = "ro-totem tokyo 2026";
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
      printf("# USED [%d]/%d for output\n", i, output);
      break;
    }
  }
  // grab MacBook Pro Microphone
  for (int i=0; i<skred_devices(1); i++) {
    if (strcmp("MacBook Pro Microphone", skred_device_str(1, i)) == 0) {
      input = skred_device_idx(1, i);
      printf("# USED [%d]/%d for input\n", i, input);
      break;
    }
  }
#endif
  skred_set_audio_device(output, input);
  skred_start(req, vc, -1);
  skred_logger(1);
  
  int r = webview_init(&webview);
  
  skoder("v0a0>1>2>3", 0);

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
