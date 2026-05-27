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

void skoder(const char *msg, char flag) {
  int r = skred_command(msg);
  char *log = skred_log();
}

static int wavepointer = 300;

void addLog(struct webview *w, char *out) {
  char res[4096];
  sprintf(res, "addLog('%s')", out);
  webview_eval(w, res);
}

static void invoker(struct webview *w, const char *arg) {
  char cmd[1024];
  switch (arg[0]) {
    case '!':
      skoder(&arg[1], 0);
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
        skoder(cmd, 0);
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
      }
      break;
    default:
      break;
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
  get_resource_path("mini-skred", bin_path);

  struct webview webview;
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = "ro-totem easter 2026";
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
  skred_set_audio_device(output, input);
  skred_start(req, vc, -1);
  skred_logger(1);


  skoder("v0a0>1>2>3", 0);
  
  int r = webview_init(&webview);
  do {
    r = webview_loop(&webview, 1);
  } while (r == 0);

  // tell it to quit...
  skoder("/q", 0);
  
  sleep(1);
  
  // Cleanup: Don't leave mini-skred hanging

  return 0;
}
