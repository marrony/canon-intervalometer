#include <libgen.h>
#include <stdio.h>

#include "camera.h"
#include "mongoose.h"
#include "timer.h"

#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_HTML "Content-Type: text/html\r\n"
#define CONTENT_TYPE_JSON "Content-Type: application/json\r\n"

static const char *s_http_addr = "http://0.0.0.0:8001"; // HTTP port
static char s_root_dir[PATH_MAX + 1] = {0};

static void not_found(struct mg_connection *c) {
  mg_http_reply(c, 404, CONTENT_TYPE_TEXT, "Not Found");
}

static void render_error(struct mg_connection *c, const char *msg) {
  mg_http_reply(c, 400, CONTENT_TYPE_TEXT, msg);
}

static size_t render_camera_content(mg_pfn_t out, void *ptr, va_list *ap) {
  size_t size = 0;

  size += mg_xprintf(out, ptr,
                     "<div id=\"camera-content\" class=\"content camera\">");
  size +=
      mg_xprintf(out, ptr,
                 "  <fieldset>"
                 "    <legend>Camera</legend>"
                 "    <input name=\"camera\" type=\"text\" disabled value=%m />"
                 "  </fieldset>",
                 MG_ESC(g_state.initialized ? g_state.description
                                            : "No cameras detected"));

  if (g_state.initialized) {
    if (g_state.connected) {
      size +=
          mg_xprintf(out, ptr,
                     "  <button hx-post=\"/api/camera/disconnect\" "
                     "hx-target=\"#content\" "
                     "    hx-swap=\"outerHTML swap:1s\">Disconnect</button>");
    } else {
      size += mg_xprintf(out, ptr,
                         "  <button hx-post=\"/api/camera/connect\" "
                         "hx-target=\"#content\" "
                         "    hx-swap=\"outerHTML swap:1s\">Connect</button>");
    }
  } else {
    size += mg_xprintf(
        out, ptr,
        "  <button hx-get=\"/api/camera\" hx-target=\"#camera-content\" "
        "    hx-swap=\"outerHTML swap:1s\">Refresh</button>");
  }

  size += mg_xprintf(out, ptr, "</div>");

  return size;
}

static void render_camera_response(struct mg_connection *c) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_camera_content);
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

static bool inputs_enabled() {
  return true || g_state.initialized && g_state.connected && !g_state.shooting;
}

static size_t render_input(mg_pfn_t out, void *ptr, va_list *ap) {
  const char *id = va_arg(*ap, const char *);
  long value = va_arg(*ap, long);
  int enabled = va_arg(*ap, int);

  return mg_xprintf(out, ptr,
                    "<input type=\"number\" name=\"%s\" value=\"%d\" min=\"0\" "
                    "  inputmode=\"decimal\" hx-post=\"/api/camera/state/%s\" "
                    "  hx-swap=\"outerHTML swap:1s\" %s />",
                    id, value, id, enabled ? "" : "disabled");
}

static size_t render_exposure(mg_pfn_t out, void *ptr, va_list *ap) {
  char value[32] = {0};

  if (g_state.exposure_ns >= 300 * MICROS) {
    float seconds = g_state.exposure_ns / (float)NANOS;
    snprintf(value, sizeof(value), "%.1f", seconds);
  } else {
    int seconds = NANOS / g_state.exposure_ns;
    snprintf(value, sizeof(value), "1/%d", seconds);
  }

  return mg_xprintf(out, ptr,
                    "<input type=\"text\" name=\"exposure\" "
                    "  pattern=\"\\d{1,3}(\\.\\d)?|1/\\d{1,5}\" value=\"%s\" "
                    "  hx-post=\"/api/camera/state/exposure\" "
                    "  hx-swap=\"outerHTML swap:1s\" %s />",
                    value, inputs_enabled() ? "" : "disabled");
}

static size_t render_inputs_content(mg_pfn_t out, void *ptr, va_list *ap) {
  bool enabled = inputs_enabled();

  return mg_xprintf(out, ptr,
                    "<div class=\"content inputs\">"
                    "  <fieldset>"
                    "    <legend>Delay (seconds)</legend>"
                    "    <div>%M</div>"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Exposure (seconds)</legend>"
                    "    <div class=\"exposure\">%M</div>"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Interval (seconds)</legend>"
                    "    <div class=\"interval\">%M</div>"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Frames</legend>"
                    "    <div class=\"frames\">%M</div>"
                    "  </fieldset>"
                    "</div>",
                    render_input, "delay", g_state.delay, enabled,
                    render_exposure, render_input, "interval", g_state.interval,
                    enabled, render_input, "frames", g_state.frames, enabled);
}

static size_t render_actions_content(mg_pfn_t out, void *ptr, va_list *ap) {
  size_t size = 0;

  size += mg_xprintf(out, ptr, "<div class=\"content actions\">");

  {
    bool enabled =
        g_state.initialized && g_state.connected && !g_state.shooting;
    size += mg_xprintf(out, ptr,
                       "<button hx-post=\"/api/camera/start-shoot\" "
                       "  hx-target=\"#content\" hx-swap=\"outerHTML "
                       "  swap:1s\" %s>Start</button>",
                       !enabled ? "disabled" : "");
  }

  {
    bool enabled = g_state.initialized && g_state.connected && g_state.shooting;
    size += mg_xprintf(out, ptr,
                       "<button hx-post=\"/api/camera/stop-shoot\" "
                       "  hx-target=\"#content\" hx-swap=\"outerHTML "
                       "  swap:1s\" %s>Stop</button>",
                       !enabled ? "disabled" : "");
  }

  {
    bool enabled =
        g_state.initialized && g_state.connected && !g_state.shooting;
    size += mg_xprintf(out, ptr,
                       "<button hx-post=\"/api/camera/take-picture\" "
                       "  hx-target=\"#content\" hx-swap=\"outerHTML "
                       "  swap:1s\" %s>Take Picture</button>",
                       !enabled ? "disabled" : "");
  }

  size += mg_xprintf(out, ptr, "</div>");

  return size;
}

static size_t render_content(mg_pfn_t out, void *ptr, va_list *ap) {
  const char *refresh =
      g_state.shooting
          ? "hx-get=\"/api/camera/state\" hx-swap=\"outerHTML swap:1s\" "
            "hx-trigger=\"every 2s\""
          : "";

  return mg_xprintf(out, ptr,
                    "<div id=\"content\" class=\"content\" %s>%M%M%M</div>",
                    refresh, render_camera_content, render_inputs_content,
                    render_actions_content);
}

// lock state for rendering
static void render_index_html_response(struct mg_connection *c,
                                       struct mg_http_message *hm) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  mg_http_reply(c, 200, CONTENT_TYPE_HTML,
                "<!doctype html>"
                "<html lang=\"en\">"
                "<head>"
                "  <meta name=\"viewport\" content=\"width=device-width, "
                "    initial-scale=1.0\" />"
                "  <link rel=\"stylesheet\" href=\"assets/index.css\">"
                "  <script src=\"assets/htmx.min.js\"></script>"
                "  <script src=\"assets/index.js\"></script>"
                "</head>"
                "<body>%M</body>"
                "</html>",
                render_content);
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

// lock state for rendering
static void render_state_response(struct mg_connection *c, bool no_content) {
  if (no_content && is_shooting()) {
    mg_http_reply(c, 204, CONTENT_TYPE_HTML, "No Content");
  } else {
    assert(pthread_mutex_lock(&g_state.mutex) == 0);
    mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_content);
    assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  }
}

// lock state for rendering
static void handle_input(struct mg_connection *c, struct mg_str *body,
                         const char *variable, long *out_value) {
  char buf[32];

  if (mg_http_get_var(body, variable, buf, sizeof(buf)) > 0) {
    pthread_mutex_lock(&g_state.mutex);

    *out_value = mg_json_get_long(mg_str_n(buf, 32), "$", -1);

    if (*out_value < 0)
      *out_value = 0;

    mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_input, variable,
                  *out_value, inputs_enabled());

    pthread_mutex_unlock(&g_state.mutex);
  } else {
    render_error(c, "Some unknown error");
  }
}

typedef void (*http_handler_fn)(struct mg_connection *,
                                struct mg_http_message *);

struct http_handler_t {
  const char *endpoint;
  http_handler_fn handler;
};

static void handle_input_delay(struct mg_connection *c,
                               struct mg_http_message *hm) {
  handle_input(c, &hm->body, "delay", &g_state.delay);
}

static void handle_input_exposure(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  char buf[32];

  if (mg_http_get_var(&hm->body, "exposure", buf, sizeof(buf)) > 0) {
    long exposure = 0;
    int32_t exposure_int = 0;
    float exposure_float = 0;

    if (sscanf(buf, "1/%d", &exposure_int) == 1) {
      exposure = NANOS / exposure_int;
    } else if (sscanf(buf, "%f", &exposure_float) == 1) {
      exposure = exposure_float * NANOS;
    } else {
      exposure = 1;
    }

    pthread_mutex_lock(&g_state.mutex);

    g_state.exposure_ns = exposure;

    mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_exposure);

    pthread_mutex_unlock(&g_state.mutex);
  } else {
    render_error(c, "Some unknown error");
  }
}

static void handle_input_interval(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  handle_input(c, &hm->body, "interval", &g_state.interval);
}

static void handle_input_frames(struct mg_connection *c,
                                struct mg_http_message *hm) {
  handle_input(c, &hm->body, "frames", &g_state.frames);
}

static void handle_get_camera(struct mg_connection *c,
                              struct mg_http_message *hm) {
  async_queue_post(&g_queue, INITIALIZE, 0, NULL, /*async*/ false);
  render_camera_response(c);
}

static void handle_get_state(struct mg_connection *c,
                             struct mg_http_message *hm) {
  render_state_response(c, true);
}

static void handle_camera_connect(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  async_queue_post(&g_queue, CONNECT, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_disconnect(struct mg_connection *c,
                                     struct mg_http_message *hm) {
  async_queue_post(&g_queue, DISCONNECT, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_start_shoot(struct mg_connection *c,
                                      struct mg_http_message *hm) {
  async_queue_post(&g_queue, START_SHOOTING, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_stop_shoot(struct mg_connection *c,
                                     struct mg_http_message *hm) {
  // fixme: need to abort timer in this thread because the main
  // thread is locked on start_timer_ns() so it will not pull
  // another command from the queue until the timer times out.
  abort_timer();

  async_queue_post(&g_queue, STOP_SHOOTING, 0, NULL, /*async*/ true);
  render_state_response(c, false);
}

static void handle_camera_take_picture(struct mg_connection *c,
                                       struct mg_http_message *hm) {
  async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_get_assets(struct mg_connection *c,
                              struct mg_http_message *hm) {
  struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
  mg_http_serve_dir(c, hm, &opts);
}

static struct http_handler_t http_handlers[] = {
    {
        .endpoint = "POST /api/camera/state/delay",
        .handler = handle_input_delay,
    },
    {
        .endpoint = "POST /api/camera/state/exposure",
        .handler = handle_input_exposure,
    },
    {
        .endpoint = "POST /api/camera/state/interval",
        .handler = handle_input_interval,
    },
    {
        .endpoint = "POST /api/camera/state/frames",
        .handler = handle_input_frames,
    },
    {
        .endpoint = "GET /api/camera/state",
        .handler = handle_get_state,
    },
    {
        .endpoint = "GET /api/camera",
        .handler = handle_get_camera,
    },
    {
        .endpoint = "POST /api/camera/connect",
        .handler = handle_camera_connect,
    },
    {
        .endpoint = "POST /api/camera/disconnect",
        .handler = handle_camera_disconnect,
    },
    {
        .endpoint = "POST /api/camera/start-shoot",
        .handler = handle_camera_start_shoot,
    },
    {
        .endpoint = "POST /api/camera/stop-shoot",
        .handler = handle_camera_stop_shoot,
    },
    {
        .endpoint = "POST /api/camera/take-picture",
        .handler = handle_camera_take_picture,
    },
    {
        .endpoint = "GET /assets/*",
        .handler = handle_get_assets,
    },
    {
        .endpoint = "GET /",
        .handler = render_index_html_response,
    },
};
static int32_t http_handlers_len =
    sizeof(http_handlers) / sizeof(struct http_handler_t);

static void evt_handler(struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data) {
  (void)fn_data;

  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    struct mg_str method_uri =
        mg_str_n(hm->method.ptr, hm->method.len + hm->uri.len + 1);

    for (int32_t i = 0; i < http_handlers_len; i++) {
      if (mg_match(method_uri, mg_str(http_handlers[i].endpoint), NULL)) {
        return http_handlers[i].handler(c, hm);
      }
    }

    return not_found(c);
  }
}

static void fix_root_dir(const char *argv) {
  char temp_dir[PATH_MAX + 1] = {0};

  realpath(argv, temp_dir);
  snprintf(s_root_dir, PATH_MAX, "%s/web_root", dirname(temp_dir));
}

void *http_server_thread(void *data) {
  const char **argv = (const char **)data;
  fix_root_dir(argv[0]);

  struct mg_mgr mgr;
  mg_log_set(MG_LL_DEBUG);
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, s_http_addr, evt_handler, NULL);
  while (g_state.running) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);

  return NULL;
}
