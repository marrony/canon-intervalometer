// Copyright (c) 2022 Cesanta Software Limited
// All rights reserved
//
// REST basics example
// It implements the following endpoints:
//    /api/f1 - respond with a simple mock result
//    /api/sum - respond with the result of adding two numbers
//    any other URI serves static files from s_root_dir
// Results are JSON strings

#include <stdlib.h>
#include <libgen.h>
#include "mongoose.h"

static const char *s_http_addr = "http://0.0.0.0:8000";  // HTTP port
static char s_root_dir[PATH_MAX+1] = {0};

static void fix_root_dir(const char* argv) {
  char temp_dir[PATH_MAX+1] = {0};

  realpath(argv, temp_dir);
  snprintf(s_root_dir, PATH_MAX, "%s/web_root", dirname(temp_dir));
}

static bool g_shooting = false;
static long g_delay = 5;
static long g_exposure = 30;
static long g_interval = 5;
static long g_frames = 50;
static long g_frames_taken = 0;

static void serialize_state(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:{%m:%s,%m:%d,%m:%d,%m:%d,%m:%d,%m:%d}}\n",
    MG_ESC("status"), MG_ESC("success"),
    MG_ESC("state"),
    MG_ESC("shooting"), g_shooting ? "true" : "false",
    MG_ESC("delay"), g_delay,
    MG_ESC("exposure"), g_exposure,
    MG_ESC("interval"), g_interval,
    MG_ESC("frames"), g_frames,
    MG_ESC("frames_taken"), g_frames_taken
  );
}

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;

    if (mg_http_match_uri(hm, "/api/cameras")) {
      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "[{\"id\": 1, \"description\": \"Canon EOS R50\"}]\n"
      );
    } else if (mg_http_match_uri(hm, "/api/camera/*/connect")) {
      serialize_state(c);
/*      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"state\": \"success\"}\n"
      );*/
    } else if (mg_http_match_uri(hm, "/api/camera/*/disconnect")) {
      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"success\"}\n"
      );
    } else if (mg_http_match_uri(hm, "/api/camera/*/start-shoot")) {
      struct mg_str json = hm->body;

      g_delay = mg_json_get_long(json, "$.delay", -1);
      g_exposure = mg_json_get_long(json, "$.exposure", -1);
      g_interval = mg_json_get_long(json, "$.interval", -1);
      g_frames = mg_json_get_long(json, "$.frames", -1);

      g_shooting = true;
      g_frames_taken = 0;

      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"success\"}\n"
      );
    } else if (mg_http_match_uri(hm, "/api/camera/*/stop-shoot")) {
      g_shooting = false;

      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"success\"}\n"
      );
    } else if (mg_http_match_uri(hm, "/api/camera/*/state")) {
      if (g_frames_taken++ > 10) {
        g_shooting = false;
        g_frames_taken = 0;
      }

      serialize_state(c);
    } else {
      struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
      mg_http_serve_dir(c, ev_data, &opts);
    }
  }
  (void) fn_data;
}

int main(int argc, char* argv[]) {
  fix_root_dir(argv[0]);

  struct mg_mgr mgr;                            // Event manager
  mg_log_set(MG_LL_DEBUG);                      // Set log level
  mg_mgr_init(&mgr);                            // Initialise event manager
  mg_http_listen(&mgr, s_http_addr, fn, NULL);  // Create HTTP listener
  for (;;) mg_mgr_poll(&mgr, 1000);             // Infinite event loop
  mg_mgr_free(&mgr);
  return 0;
}
