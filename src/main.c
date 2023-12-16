#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "http.h"

static pthread_t http_server;
static pthread_t main_thread;

void print_help(char *cmdline) {
  printf("Usage: %s [options]\n", cmdline);
  printf("\n");
  printf("Canon Intervalometer for Raspberry PI\n");
  printf("\n");
  printf("Options:\n");
  printf("  -w, --web-root <path> Web root folder\n");
  printf("  -h, --help            Dislay help\n");
}

static char web_root[PATH_MAX] = {0};

int main(int argc, char *argv[]) {
  const char *short_options = "h::w::";
  const struct option long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"web-root", required_argument, NULL, 'w'},
  };

  int next_option;
  do {
    next_option = getopt_long(argc, argv, short_options, long_options, NULL);

    switch (next_option) {
    case 'w':
      strncpy(web_root, optarg, PATH_MAX);
      break;

    case '?':
    case 'h':
      print_help(argv[0]);
      return EXIT_SUCCESS;
    }
  } while (next_option != -1);

  if (strlen(web_root) == 0) {
    print_help(argv[0]);
    return EXIT_FAILURE;
  }

  main_thread = pthread_self();

  pthread_create(&http_server, NULL, http_server_thread, web_root);

  // EDSDK demands its api call to be in the main thread on MacOS
  command_processor();

  pthread_join(http_server, NULL);

  return EXIT_SUCCESS;
}
