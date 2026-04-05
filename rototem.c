#define WEBVIEW_IMPLEMENTATION
/* don't forget to define WEBVIEW_WINAPI,WEBVIEW_GTK or WEBVIEW_COCAO */
#include "webview.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h> // Required for close()
#include <string.h>

void udp_send(const char *ip, int port, const char *msg) {
    char out[1024];
    sprintf(out, ";%s;", msg);
    printf("# UDP SEND %s %d %s\n", ip, port, out);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(ip)
    };

    sendto(s, msg, strlen(msg), 0, (struct sockaddr*)&addr, sizeof(addr));
    
    close(s); // Properly release the file descriptor
}

float vol[4] = {0};
float mvol = -20.0;
float frq[4] = {0};

static int wavepointer = 300;
#define ADDR "127.0.0.1"
#define PORT 60440
static void doit(struct webview *w, const char *arg) {
  printf("called with '%s'\n", arg);
  char out[1024];
  switch (arg[0]) {
    case '!':
      //webview_eval(w, "alert('hello');");
      udp_send(ADDR, PORT, &arg[1]);
      break;
    case '$':
      printf("udp to skred {%s}\n", &arg[1]);
      break;
    case '@':
      {
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_DIRECTORY, "sel", "", res, sizeof(res));
        if (1) {
          sprintf(out, "assign('%s','{%s}');", "dir", res);
          webview_eval(w, out);
        }
      }
      break;
    case '[':
      if (arg[1] >= '0' && arg[1] <= '3') {
        int vint = arg[1] - '0';
        frq[vint] /= 2.0;
        sprintf(out, "v%d f%g", vint, frq[vint]);
        udp_send(ADDR, PORT, out);
      }
      break;
    case ']':
      if (arg[1] >= '0' && arg[1] <= '3') {
        int vint = arg[1] - '0';
        frq[vint] *= 2.0;
        sprintf(out, "v%d f%g", vint, frq[vint]);
        udp_send(ADDR, PORT, out);
      }
      break;
    case '(':
      {
        mvol -= 1.0;
        sprintf(out, "V%g", mvol);
        udp_send(ADDR, PORT, out);
      }
      break;
    case ')':
      {
        mvol += 1.0;
        sprintf(out, "V%g", mvol);
        udp_send(ADDR, PORT, out);
      }
      break;
    case '+':
      if (arg[1] >= '0' && arg[1] <= '3') {
        int vint = arg[1] - '0';
        vol[vint] += 1.0;
        sprintf(out, "v%d a%g", vint, vol[vint]);
        udp_send(ADDR, PORT, out);
      }
      break;
    case '-':
      if (arg[1] >= '0' && arg[1] <= '3') {
        int vint = arg[1] - '0';
        vol[vint] -= 1.0;
        sprintf(out, "v%d a%g", vint, vol[vint]);
        udp_send(ADDR, PORT, out);
      }
      break;
    case '>':
      if (arg[1] == 'v') {
        const char *voice = &arg[1];
        char res[1024];
        webview_dialog(w, WEBVIEW_DIALOG_TYPE_OPEN, WEBVIEW_DIALOG_FLAG_FILE, "sel", "", res, sizeof(res));
        if (1) {
          int vint = arg[2] - '0';
          sprintf(out, "{%s} /ws%d v%d w%d a0 B1 f440 t1 0 1 1", res, wavepointer, vint, wavepointer);
          wavepointer++;
          if (wavepointer > 999) wavepointer = 0;
          printf("# to skred -> %s\n", out);
          udp_send(ADDR, PORT, out);
          int len = strlen(res);
          char *ptr = res;
          for (int i=len; i>0; i--) {
            if (ptr[i-1] == '/') {
              ptr += i;
              break;
            }
          }
          sprintf(out, "assign('%s','{%s}');", voice, ptr);
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
  for (int i=0; i<4; i++) {
    frq[i] = 440.0;
    vol[i] = 0.0;
  }
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
  webview.height = 730;
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
