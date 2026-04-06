#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
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

typedef struct {
  pid_t pid;
  FILE *to_child;   // stdin of helper
  FILE *from_child; // stdout of helper
} talker_t;

talker_t *launch_line_buffered_helper(const char *path, char **argv) {
  talker_t *hp = (talker_t*)calloc(1, sizeof(talker_t));
  memset(hp, 0, sizeof(talker_t));
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

  extern char **environ;

  if (posix_spawn(&hp->pid, path, &actions, NULL, argv, environ) == 0) {
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

talker_t *skred = NULL;

void skoder(const char *msg, char flag) {
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
        sprintf(cmd, "{%s} /ws%d v%d w%d a0 B1 f440 t1 0 1 1", filename, wavepointer, voice, wavepointer);
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
  
  char skred_path[1024];
  get_bundle_resource_path("mini-skred", skred_path, sizeof(skred_path));

  // trevor's rototem port
  // Define the arguments: -n and -p60472
  char *sargv[] = { skred_path, "-n", "-p60472", "-v16", NULL };

  skred = launch_line_buffered_helper(skred_path, sargv);

  if (skred->pid > 0) {
    skoder("v0a0>1>2>3", 0);
  } else {
    puts("FAIL");
    exit(1);
  }
  
  int r = webview_init(&webview);
  do {
    r = webview_loop(&webview, 1);
  } while (r == 0);

  // tell it to quit...
  skoder("/q", 0);
  
  sleep(1);
  
  // Cleanup: Don't leave mini-skred hanging
  kill(skred->pid, SIGTERM);
  waitpid(skred->pid, NULL, 0);
  fclose(skred->to_child);
  fclose(skred->from_child);

  return 0;
}
