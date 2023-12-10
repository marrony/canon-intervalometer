#include <signal.h>

#include "camera.h"
#include "http.h"

static pthread_t http_server;
static pthread_t main_thread;

static void sig_handler(int sig) {
  async_queue_post(&g_queue, DISCONNECT, 0, NULL, true);
  async_queue_post(&g_queue, TERMINATE, 0, NULL, true);
}

int main(int argc, const char *argv[]) {
  signal(SIGTERM, sig_handler);
  signal(SIGINT, sig_handler);

  main_thread = pthread_self();

#if 0
  struct sched_param param = {0};
  param.sched_priority = 99;
  pthread_setschedparam(main_thread, SCHED_FIFO, &param);
#endif

  pthread_create(&http_server, NULL, http_server_thread, argv);

  // EDSDK demands its api call to be in the main thread on MacOS
  command_processor();

  pthread_join(http_server, NULL);

  return 0;
}
