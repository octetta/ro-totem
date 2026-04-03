#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

static int wavepointer = 300;
static void doit(struct webview *w, const char *arg) {
  printf("Callback called with '%s'\n", arg);
  switch (arg[0]) {
    case '!':
      webview_eval(w, "alert('hello');");
      break;
    case '$':
      printf("udp to skred {%s}\n", &arg[1]);
      break;
    case '@':
      {
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_DIRECTORY, "sel", "", res, sizeof(res));
        if (1) {
          char out[1024];
          sprintf(out, "assign('%s','{%s}');", "dir", res);
          webview_eval(w, out);
        }
      }
      break;
    case '>':
      if (arg[1] == 'v') {
        const char *voice = &arg[1];
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE, "sel", "", res, sizeof(res));
        if (1) {
          char out[1024];
          sprintf(out, "{%s} /ws%d v0 w%d a0B1f440l1", res, wavepointer, wavepointer);
          wavepointer++;
          if (wavepointer > 999) wavepointer = 0;
          printf("# to skred -> %s\n", out);
          sprintf(out, "assign('%s','{%s}');", voice, res);
          webview_eval(w, out);
        }
      }
      break;
    default:
      printf("unknown {%s}\n", &arg[1]);
      break;
  }
}

#define FILE_URL "file://"

int main(int argc, char *argv[]) {
  {
    char path[PATH_MAX];
    size_t size;
    #ifdef __APPLE__
    _NSGetExecutablePath(path, &size);
    #elif __linux__
    readlink("/proc/self/exe", path, size);
    #endif
    printf("# exe{%s}\n", path);
    char real_path[PATH_MAX];
    realpath(path, real_path);
    printf("# exe{%s}\n", real_path);
  }
  char path[PATH_MAX];
  realpath("ui.html", path);
  char url[PATH_MAX + sizeof(FILE_URL)];
  snprintf(url, sizeof(url), FILE_URL "%s", path);
  printf("# url{%s}\n", url);
  printf("# path{%s}\n", path);
  struct webview webview;
  int r;
  memset(&webview, 0, sizeof(webview));
  webview.url = url;
#if 0
  webview.url = \
  "data:text/html,<!DOCTYPE html><html><body>"
    "<p id=\"sentence\">It works !</p>"
    "<button onclick=\"window.external.invoke('Hi')\">Callback</button>"
    "<input type=\"file\" id=\"dirPicker\" webkitdirectory style=\"display:none;\" />"
    "<button type=\"button\" onclick=\"document.getElementById('dirPicker').click()\">"
    "  Select Directory"
    "  </button>"
    "</body></html>";
#endif
  webview.title = "rototem";
  webview.width = 800;
  webview.height = 600;
  webview.resizable = 1;
  webview.debug = 1;
  webview.external_invoke_cb = &doit;
  printf("before init\n");
  r = webview_init(&webview);
  int n = 0;
  printf("before loop\n");
  do {
    r = webview_loop(&webview, 1);
    //printf("loop %d\n", n++);
  } while (r == 0);
  printf("before exit\n");
  return 0;
}
