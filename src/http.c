#include <libgen.h>
#include <stdio.h>

#include "camera.h"
#include "mongoose.h"
#include "queue.h"
#include "timer.h"

typedef void (*http_handler_fn)(struct mg_connection *,
                                struct mg_http_message *);

struct http_handler_t {
  const char *endpoint;
  http_handler_fn handler;
};

#define CONTENT_TYPE_TEXT "Content-Type: text/plain\r\n"
#define CONTENT_TYPE_HTML "Content-Type: text/html\r\n"
#define CONTENT_TYPE_JSON "Content-Type: application/json\r\n"

static const char *s_http_addr = "http://0.0.0.0:8001"; // HTTP port
static char s_root_dir[PATH_MAX + 1] = {0};

static void not_found(struct mg_connection *c) {
  mg_http_reply(c, 404, CONTENT_TYPE_TEXT, "Not Found");
}

#if 0
static void render_error(struct mg_connection *c, const char *msg) {
  mg_http_reply(c, 400, CONTENT_TYPE_TEXT, msg);
}
#endif

static size_t render_camera_content(mg_pfn_t out, void *ptr, va_list *ap) {
  const struct camera_state_t *state =
      va_arg(*ap, const struct camera_state_t *);

  size_t size = 0;

  size += mg_xprintf(out, ptr,
                     "<div id=\"camera-content\" class=\"content camera\">");
  size += mg_xprintf(
      out, ptr,
      "  <fieldset>"
      "    <legend>Camera</legend>"
      "    <input name=\"camera\" type=\"text\" disabled value=%m />"
      "  </fieldset>",
      MG_ESC(state->initialized ? state->description : "No cameras detected"));

  if (state->initialized) {
    if (state->connected) {
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
  struct camera_state_t state;
  get_copy_state(&state);

  mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_camera_content, state);
}

static bool inputs_enabled(const struct camera_state_t *state) {
  return true || (state->initialized && state->connected && !state->shooting);
}

static size_t render_input(mg_pfn_t out, void *ptr, va_list *ap) {
  const char *id = va_arg(*ap, const char *);
  long value = va_arg(*ap, long);
  int enabled = va_arg(*ap, int);

  return mg_xprintf(
      out, ptr,
      "<input type=\"number\" name=\"%s\" value=\"%d\" required min=\"0\" "
      "  inputmode=\"decimal\" hx-post=\"/api/camera/state/%s\" "
      "  hx-swap=\"outerHTML swap:1s\" %s />",
      id, value, id, enabled ? "" : "disabled");
}

static size_t render_exposure(mg_pfn_t out, void *ptr, va_list *ap) {
  const struct camera_state_t *state =
      va_arg(*ap, const struct camera_state_t *);

  char value[32] = {0};

  get_exposure(value, sizeof(value));

  return mg_xprintf(
      out, ptr,
      "<input type=\"text\" name=\"exposure\" required hx-validate=\"true\" "
      "  pattern=\"\\d{1,3}(\\.\\d)?|1/\\d{1,5}\" value=\"%s\" "
      "  hx-post=\"/api/camera/state/exposure\" "
      "  hx-swap=\"outerHTML swap:1s\" %s />",
      value, inputs_enabled(state) ? "" : "disabled");
}

static size_t render_inputs_content(mg_pfn_t out, void *ptr, va_list *ap) {
  const struct camera_state_t *state =
      va_arg(*ap, const struct camera_state_t *);

  bool enabled = inputs_enabled(state);

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
                    render_input, "delay", state->delay, enabled,
                    render_exposure, state, render_input, "interval",
                    state->interval, enabled, render_input, "frames",
                    state->frames, enabled);
}

static size_t render_actions_content(mg_pfn_t out, void *ptr, va_list *ap) {
  const struct camera_state_t *state =
      va_arg(*ap, const struct camera_state_t *);

  size_t size = 0;

  size += mg_xprintf(out, ptr, "<div class=\"content actions\">");

  {
    bool enabled = state->initialized && state->connected && !state->shooting;
    size += mg_xprintf(out, ptr,
                       "<button hx-post=\"/api/camera/start-shoot\" "
                       "  hx-target=\"#content\" hx-swap=\"outerHTML "
                       "  swap:1s\" %s>Start</button>",
                       !enabled ? "disabled" : "");
  }

  {
    bool enabled = state->initialized && state->connected && state->shooting;
    size += mg_xprintf(out, ptr,
                       "<button hx-post=\"/api/camera/stop-shoot\" "
                       "  hx-target=\"#content\" hx-swap=\"outerHTML "
                       "  swap:1s\" %s>Stop</button>",
                       !enabled ? "disabled" : "");
  }

  {
    bool enabled = state->initialized && state->connected && !state->shooting;
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
  const struct camera_state_t *state =
      va_arg(*ap, const struct camera_state_t *);

  const char *refresh =
      state->shooting
          ? "hx-get=\"/api/camera/state\" hx-swap=\"outerHTML swap:1s\" "
            "hx-trigger=\"every 2s\""
          : "";

  return mg_xprintf(
      out, ptr, "<div id=\"content\" class=\"content\" %s>%M%M%M</div>",
      refresh, render_camera_content, state, render_inputs_content, state,
      render_actions_content, state);
}

static void render_index_html_response(struct mg_connection *c,
                                       struct mg_http_message *hm) {
  struct camera_state_t state;
  get_copy_state(&state);

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
                render_content, &state);
}

static void render_state_response(struct mg_connection *c, bool no_content) {
  struct camera_state_t state;
  get_copy_state(&state);

  if (no_content && state.shooting) {
    mg_http_reply(c, 204, CONTENT_TYPE_HTML, "No Content");
  } else {
    mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_content, &state);
  }
}

static void render_input_response(struct mg_connection *c, const char *variable,
                                  long value, bool enabled) {
  mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_input, variable, value,
                enabled);
}

static void render_exposure_response(struct mg_connection *c,
                                     const struct camera_state_t *state) {
  mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_exposure, state);
}

static void handle_input_exposure(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  char buf[32];
  if (mg_http_get_var(&hm->body, "exposure", buf, sizeof(buf)) > 0)
    set_exposure(buf);

  struct camera_state_t state;
  get_copy_state(&state);
  render_exposure_response(c, &state);
}

static void handle_input_delay(struct mg_connection *c,
                               struct mg_http_message *hm) {
  char buf[32];
  if (mg_http_get_var(&hm->body, "delay", buf, sizeof(buf)) > 0)
    set_delay(buf);

  struct camera_state_t state;
  get_copy_state(&state);
  render_input_response(c, "delay", state.delay, inputs_enabled(&state));
}

static void handle_input_interval(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  char buf[32];
  if (mg_http_get_var(&hm->body, "interval", buf, sizeof(buf)) > 0)
    set_interval(buf);

  struct camera_state_t state;
  get_copy_state(&state);
  render_input_response(c, "interval", state.interval, inputs_enabled(&state));
}

static void handle_input_frames(struct mg_connection *c,
                                struct mg_http_message *hm) {
  char buf[32];
  if (mg_http_get_var(&hm->body, "frames", buf, sizeof(buf)) > 0)
    set_frames(buf);

  struct camera_state_t state;
  get_copy_state(&state);
  render_input_response(c, "frames", state.frames, inputs_enabled(&state));
}

static void handle_get_camera(struct mg_connection *c,
                              struct mg_http_message *hm) {
  async_queue_post(&g_main_queue, INITIALIZE, 0, NULL, /*async*/ false);
  render_camera_response(c);
}

static void handle_get_state(struct mg_connection *c,
                             struct mg_http_message *hm) {
  render_state_response(c, true);
}

static void handle_camera_connect(struct mg_connection *c,
                                  struct mg_http_message *hm) {
  async_queue_post(&g_main_queue, CONNECT, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_disconnect(struct mg_connection *c,
                                     struct mg_http_message *hm) {
  async_queue_post(&g_main_queue, DISCONNECT, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_start_shoot(struct mg_connection *c,
                                      struct mg_http_message *hm) {
  async_queue_post(&g_main_queue, START_SHOOTING, 0, NULL, /*async*/ false);
  render_state_response(c, false);
}

static void handle_camera_stop_shoot(struct mg_connection *c,
                                     struct mg_http_message *hm) {
  // fixme: need to abort timer in this thread because the main
  // thread is locked on start_timer_ns() so it will not pull
  // another command from the queue until the timer times out.
  abort_timer();

  async_queue_post(&g_main_queue, STOP_SHOOTING, 0, NULL, /*async*/ true);
  render_state_response(c, false);
}

static void handle_camera_take_picture(struct mg_connection *c,
                                       struct mg_http_message *hm) {
  async_queue_post(&g_main_queue, TAKE_PICTURE, 0, NULL, /*async*/ false);
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
  while (is_running()) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);

  return NULL;
}
