#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>

void get_resource_path(const char *filename, char *out_path) {
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
  char path[PATH_MAX];
    
  if (CFURLGetFileSystemRepresentation(resourcesURL, true, (UInt8 *)path, PATH_MAX)) {
    snprintf(out_path, PATH_MAX, "%s/%s", path, filename);
  }

  CFRelease(resourcesURL);
}

typedef struct {
  pid_t pid;
  FILE *to_child;   // stdin of helper
  FILE *from_child; // stdout of helper
} HelperProcess;

HelperProcess *launch_line_buffered_helper(const char *path, char **argv) {
  HelperProcess *hp = (HelperProcess*)calloc(1, sizeof(HelperProcess));
  memset(hp, 0, sizeof(HelperProcess));
  int p_to_c[2], c_to_p[2];
  pipe(p_to_c);
  pipe(c_to_p);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
    
  // Map pipes to standard streams
  posix_spawn_file_actions_adddup2(&actions, p_to_c[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, c_to_p[1], STDOUT_FILENO);
    
  // Close unused ends in child
  posix_spawn_file_actions_addclose(&actions, p_to_c[1]);
  posix_spawn_file_actions_addclose(&actions, c_to_p[0]);

  if (posix_spawn(&hp->pid, path, &actions, NULL, argv, NULL) == 0) {
    close(p_to_c[0]);
    close(c_to_p[1]);

    // Convert raw descriptors to line-buffered FILE streams
    hp->to_child = fdopen(p_to_c[1], "w");
    hp->from_child = fdopen(c_to_p[0], "r");

    setvbuf(hp->to_child, NULL, _IOLBF, 0);
    setvbuf(hp->from_child, NULL, _IOLBF, 0);
  }

  posix_spawn_file_actions_destroy(&actions);
  return hp;
}

void get_bundle_resource_path(const char *filename, char *out_path, int max_len) {
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  CFURLRef resURL = CFBundleCopyResourceURL(mainBundle, 
    CFStringCreateWithCString(NULL, filename, kCFStringEncodingUTF8), NULL, NULL);
  if (!resURL) {
    return;
  }
  CFURLGetFileSystemRepresentation(resURL, true, (UInt8 *)out_path, max_len);
  CFRelease(resURL);
}

HelperProcess *skred = NULL;

void skoder(const char *msg) {
  if (skred) {
    fprintf(skred->to_child, "%s\n", msg);
  }
}

static int wavepointer = 300;

void addLog(struct webview *w, char *out) {
  char res[4096];
  sprintf(res, "addLog('%s')", out);
  webview_eval(w, res);
}

static void invoker(struct webview *w, const char *arg) {
  char out[1024];
  switch (arg[0]) {
    case '!':
      skoder(&arg[1]);
      break;
    case '@':
      {
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_DIRECTORY, "sel", "", res, sizeof(res));
        webview_eval(w, "lclear()");
        sprintf(out, "assign('dir','%s');", res);
        webview_eval(w, out);
        struct dirent *entry;
        DIR *dp = opendir(res);
        if (dp) {
          while ((entry = readdir(dp))) {
            char *name = entry->d_name;
            size_t len = strlen(name);
            if ((len > 4) && (strcasecmp(name + len - 4, ".wav") == 0)) {
              sprintf(res, "lstuff('%s')", name);
              webview_eval(w, res);
            }
          }
        }
      }
      break;
    case '>':
      if (arg[1] == 'v') {
        const char *voice = &arg[1];
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE, "sel", "", res, sizeof(res));
        int vint = arg[2] - '0';
        sprintf(out, "{%s} /ws%d v%d w%d a0 B1 f440 t1 0 1 1", res, wavepointer, vint, wavepointer);
        addLog(w, out);
        wavepointer++;
        if (wavepointer > 999) wavepointer = 0;
        skoder(out);
        int len = strlen(res);
        char *ptr = res;
        for (int i=len; i>0; i--) {
          if (ptr[i-1] == '/') {
            ptr += i;
            break;
          }
        }
        sprintf(out, "assign('%s','%s');", voice, ptr);
        webview_eval(w, out);
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
  get_resource_path("mini-skred", bin_path);
  {
    char path[PATH_MAX];
    size_t size;
    #ifdef __APPLE__
    _NSGetExecutablePath(path, &size);
    #elif __linux__
    readlink("/proc/self/exe", path, size);
    #endif
    char real_path[PATH_MAX];
    realpath(path, real_path);
  }
  char path[PATH_MAX];
  realpath("ui.html", path);
  char url[PATH_MAX + sizeof(FILE_URL)];
  snprintf(url, sizeof(url), FILE_URL "%s", path);
  struct webview webview;
  int r;
  memset(&webview, 0, sizeof(webview));
  webview.url = html_path;
  webview.title = "ro-totem easter 2026";
  webview.width = 884;  // window.innerWidth
  webview.height = 700; // window.innerHeight
  webview.resizable = 1;
  webview.debug = 1;
  webview.external_invoke_cb = &invoker;
  
  char skred_path[1024];
  get_bundle_resource_path("mini-skred", skred_path, sizeof(skred_path));

  // trevor's rototem port
  // Define the arguments: -n and -p60472
  char *sargv[] = { skred_path, "-n", "-p60472", "-v16", NULL };

  skred = launch_line_buffered_helper(skred_path, sargv);

  if (skred->pid > 0) {
    skoder("v0a0>1>2>3\n");
  } else {
    puts("FAIL");
    exit(1);
  }
  
  r = webview_init(&webview);
  int n = 0;
  do {
    r = webview_loop(&webview, 1);
  } while (r == 0);

  // tell it to quit...
  skoder("/q");
  
  sleep(1);
  
  // Cleanup: Don't leave mini-skred hanging
  kill(skred->pid, SIGTERM);
  waitpid(skred->pid, NULL, 0);
  fclose(skred->to_child);
  fclose(skred->from_child);

  return 0;
}
