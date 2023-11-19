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

static bool g_connected = false;
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

static void evt_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  (void) fn_data;

  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    struct mg_str method = hm->method;

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (is_get && mg_http_match_uri(hm, "/api/cameras")) {
      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "[{\"id\": 1, \"description\": \"Canon EOS R50\"}]\n"
      );
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/*/connect")) {
      if (g_connected) {
        mg_http_reply(
          c, 200,
          "Content-Type: application/json\r\n"
          "Access-Control-Allow-Origin: *\r\n",
          "{%m:%m,%m:%m}\n",
          MG_ESC("status"), MG_ESC("failure"),
          MG_ESC("description"), MG_ESC("Already connected")
        );
      } else {
        g_connected = true;
        serialize_state(c);
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/*/disconnect")) {
      g_connected = false;

      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"success\"}\n"
      );
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/*/start-shoot")) {
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
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/*/stop-shoot")) {
      g_shooting = false;

      mg_http_reply(
        c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"success\"}\n"
      );
    } else if (is_get && mg_http_match_uri(hm, "/api/camera/*/state")) {
      if (g_frames_taken++ > 10) {
        g_shooting = false;
        g_frames_taken = 0;
      }

      serialize_state(c);
    } else {
      mg_http_reply(
        c, 404,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"status\": \"failure\"}\n"
      );
    }
  }
}

int main(int argc, char* argv[]) {
  fix_root_dir(argv[0]);

  struct mg_mgr mgr;
  mg_log_set(MG_LL_DEBUG);
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, s_http_addr, evt_handler, NULL);
  for (;;) mg_mgr_poll(&mgr, 1000);
  mg_mgr_free(&mgr);
  return 0;
}
