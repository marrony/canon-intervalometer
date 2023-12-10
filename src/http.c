#include <libgen.h>

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

static size_t render_input(mg_pfn_t out, void *ptr, va_list *ap) {
  const char *id = va_arg(*ap, const char *);
  long value = va_arg(*ap, long);
  int enabled = va_arg(*ap, int);

  return mg_xprintf(out, ptr,
                    "<input type=\"number\" name=\"%s\" value=\"%d\" "
                    "  inputmode=\"numeric\" hx-post=\"/api/state/%s\" "
                    "  hx-swap=\"outerHTML swap:1s\" %s />",
                    id, value, id, enabled ? "" : "disabled");
}

static size_t render_inputs_content(mg_pfn_t out, void *ptr, va_list *ap) {
  bool enabled = g_state.initialized && g_state.connected && !g_state.shooting;

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
                    render_input, "exposure", g_state.exposure, enabled,
                    render_input, "interval", g_state.interval, enabled,
                    render_input, "frames", g_state.frames, enabled);
}

static size_t render_actions_content(mg_pfn_t out, void *ptr, va_list *ap) {
  size_t size = 0;

  size += mg_xprintf(out, ptr, "<div class=\"content actions\">");

  {
    bool enabled = g_state.initialized && g_state.connected && !g_state.shooting;
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
    bool enabled = g_state.initialized && g_state.connected && !g_state.shooting;
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
static void render_index_html_response(struct mg_connection *c) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  mg_http_reply(c, 200, CONTENT_TYPE_HTML,
                "<!doctype html>"
                "<html lang=\"en\">"
                "<head>"
                "  <meta name=\"viewport\" content=\"width=device-width, "
                "    initial-scale=1.0\" />"
                "  <link rel=\"stylesheet\" href=\"index.css\">"
                "  <script src=\"htmx.min.js\"></script>"
                "  <script src=\"index.js\"></script>"
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
static void handle_input(struct mg_connection *c,
                         const struct mg_http_message *hm,
                         const char *variable,
                         long *out_value) {
  struct mg_str body = hm->body;

  MG_DEBUG(("Body = %.*s", body.len, body.ptr));

  char buf[32];

  if (mg_http_get_var(&body, variable, buf, sizeof(buf)) > 0) {
    pthread_mutex_lock(&g_state.mutex);

    *out_value = mg_json_get_long(mg_str_n(buf, 32), "$", -1);

    if (*out_value < 0)
      *out_value = 0;

    mg_http_reply(c, 200, CONTENT_TYPE_HTML, "%M", render_input, variable, *out_value, g_state.shooting);

    pthread_mutex_unlock(&g_state.mutex);
  } else {
    render_error(c, "Some unknown error");
  }
}

static void evt_handler(struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data) {
  (void)fn_data;

  if (ev == MG_EV_HTTP_MSG) {
    const struct mg_http_message *hm = (const struct mg_http_message *)ev_data;
    const struct mg_str method = hm->method;

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (is_post && mg_http_match_uri(hm, "/api/state/delay")) {
      return handle_input(c, hm, "delay", &g_state.delay);
    }

    if (is_post && mg_http_match_uri(hm, "/api/state/exposure")) {
      return handle_input(c, hm, "exposure", &g_state.exposure);
    }

    if (is_post && mg_http_match_uri(hm, "/api/state/interval")) {
      return handle_input(c, hm, "interval", &g_state.interval);
    }

    if (is_post && mg_http_match_uri(hm, "/api/state/frames")) {
      return handle_input(c, hm, "frames", &g_state.frames);
    }

    if (is_get && mg_http_match_uri(hm, "/api/camera")) {
      async_queue_post(&g_queue, INITIALIZE, 0, NULL, false);
      return render_camera_response(c);
    }

    if (is_post && mg_http_match_uri(hm, "/api/camera/connect")) {
      async_queue_post(&g_queue, CONNECT, 0, NULL, false);
      return render_state_response(c, false);
    }

    if (is_post && mg_http_match_uri(hm, "/api/camera/disconnect")) {
      async_queue_post(&g_queue, DISCONNECT, 0, NULL, false);
      return render_state_response(c, false);
    }

    if (is_post && mg_http_match_uri(hm, "/api/camera/start-shoot")) {
      async_queue_post(&g_queue, START_SHOOTING, 0, NULL, false);
      return render_state_response(c, false);
    }

    if (is_post && mg_http_match_uri(hm, "/api/camera/stop-shoot")) {
      // fixme: need to abort timer in this thread because the main
      // thread is locked on start_timer_ns() so it will not pull
      // another command from the queue until the timer times out.
      abort_timer();

      async_queue_post(&g_queue, STOP_SHOOTING, 0, NULL, true);
      return render_state_response(c, false);
    }

    if (is_post && mg_http_match_uri(hm, "/api/camera/take-picture")) {
      async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, false);
      return render_state_response(c, false);
    }

    if (is_get && mg_http_match_uri(hm, "/api/camera/state")) {
      return render_state_response(c, true);
    }

    if (is_get) {
      MG_DEBUG(("GET %.*s", hm->uri.len, hm->uri.ptr));

      if (mg_http_match_uri(hm, "/")) {
        render_index_html_response(c);
      } else {
        struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
        mg_http_serve_dir(c, ev_data, &opts);
      }
    } else {
      not_found(c);
    }
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
