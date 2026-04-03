#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

static void invoke_cb(struct webview *w, const char *arg) {
  printf("Callback called with '%s'\n", arg);
  switch (arg[0]) {
    case '!':
      webview_eval(w, "alert('hello');");
      break;
    case '$':
      printf("udp to skred {%s}\n", &arg[1]);
      break;
    default:
      printf("unknown {%s}\n", &arg[1]);
      break;
  }
}

#define FILE_URL "file://"
int main(int argc, char *argv[]) {
  char path[PATH_MAX];
  realpath("ui.html", path);
  char url[PATH_MAX + sizeof(FILE_URL)];
  snprintf(url, sizeof(url), FILE_URL "%s", path);
  printf("{%s} {%s}\n", path, url);
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
  webview.external_invoke_cb = &invoke_cb;
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
