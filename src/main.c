#include <EDSDK.h>
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "mongoose.h"
#include "queue.h"
#include "timer.h"

#ifdef __MACOS__
extern __uint64_t __thread_selfid(void);
#endif

static int64_t get_system_nanos() {
  struct timespec ts = {0, 0};
#if defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#elif defined(CLOCK_MONOTONIC)
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  return (int64_t)ts.tv_sec * NANOS + (int64_t)ts.tv_nsec;
}

static struct {
  pthread_mutex_t mutex;
  bool running;
  long delay;
  long exposure;
  long interval;
  long frames;
  long frames_taken;
  bool initialized;
  bool connected;
  bool shooting;
  char description[EDS_MAX_NAME];
  EdsCameraRef camera;
} g_state = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .running = true,
    .delay = 1,
    .exposure = 5,
    .interval = 1,
    .frames = 2,
    .frames_taken = 0,
    .initialized = false,
    .connected = false,
    .shooting = false,
    .description = {0},
    .camera = NULL,
};

static struct sync_queue_t g_queue = {
    .queue = QUEUE_INITIALIZER,
    .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sync_wait = PTHREAD_COND_INITIALIZER,
    .processed = 0,
};

static bool is_initialized() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.initialized;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

static bool is_connected() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.connected;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

static bool is_shooting() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.shooting;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

static pthread_t http_server;
static pthread_t main_thread;

#if 0
static int nssleep(int64_t timer_ns) {
  struct timespec ts = {
    .tv_sec = timer_ns / NANOS,
    .tv_nsec = timer_ns % NANOS
  };
  struct timespec rem = {0, 0};

  while (nanosleep(&ts, &rem) < 0) {
    if (errno != EINTR)
      return -1;
    ts = rem;
  }

  return 0;
}
#endif

static const char *s_http_addr = "http://0.0.0.0:8001"; // HTTP port
static char s_root_dir[PATH_MAX + 1] = {0};

static void fix_root_dir(const char *argv) {
  char temp_dir[PATH_MAX + 1] = {0};

  realpath(argv, temp_dir);
  snprintf(s_root_dir, PATH_MAX, "%s/web_root", dirname(temp_dir));
}

static size_t print_camera(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr, "{%m:%m,%m:%d}", MG_ESC("description"),
                    MG_ESC(g_state.description), MG_ESC("handle"),
                    g_state.camera);
}

static size_t print_state(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(
      out, ptr, "{%m:%s,%m:%s,%m:%ld,%m:%ld,%m:%ld,%m:%ld,%m:%ld}",
      MG_ESC("shooting"), g_state.shooting ? "true" : "false",
      MG_ESC("connected"), g_state.connected ? "true" : "false",
      MG_ESC("delay"), g_state.delay, MG_ESC("exposure"), g_state.exposure,
      MG_ESC("interval"), g_state.interval, MG_ESC("frames"), g_state.frames,
      MG_ESC("frames_taken"), g_state.frames_taken);
}

static void serialize_state(struct mg_connection *c) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "{%m:%m,%m:%M}\n", MG_ESC("status"), MG_ESC("success"),
                MG_ESC("state"), print_state);
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

static void serialize_camera(struct mg_connection *c) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "{%m:%m,%m:%M,%m:%M}\n", MG_ESC("status"), MG_ESC("success"),
                MG_ESC("camera"), print_camera, MG_ESC("state"), print_state);
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

static void serialize_success(struct mg_connection *c) {
  mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "{%m:%m}\n", MG_ESC("status"), MG_ESC("success"));
}

static void serialize_failure(struct mg_connection *c,
                              const char *description) {
  mg_http_reply(c, 200,
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "{%m:%m,%m:%m}\n", MG_ESC("status"), MG_ESC("failure"),
                MG_ESC("description"), MG_ESC(description));
}

static void not_found(struct mg_connection *c) {
  mg_http_reply(c, 404, "Access-Control-Allow-Origin: *\r\n", "Not Found");
}

static bool detect_connected_camera() {
  EdsCameraListRef camera_list = NULL;

  if (EdsGetCameraList(&camera_list) == EDS_ERR_OK) {
    EdsUInt32 count = 0;

    if (EdsGetChildCount(camera_list, &count) == EDS_ERR_OK) {
      MG_DEBUG(("Camera count: %d", count));

      if (count != 1) {
        EdsRelease(camera_list);
        return false;
      }

      if (g_state.camera != NULL) {
        EdsRelease(g_state.camera);
        g_state.camera = NULL;
      }

      EdsCameraRef camera_ref = NULL;
      EdsDeviceInfo device_info;

      if (EdsGetChildAtIndex(camera_list, 0, &camera_ref) == EDS_ERR_OK) {
        if (camera_ref != NULL &&
            EdsGetDeviceInfo(camera_ref, &device_info) == EDS_ERR_OK) {
          g_state.camera = camera_ref;
          strncpy(g_state.description, device_info.szDeviceDescription,
                  EDS_MAX_NAME);
        } else {
          EdsRelease(camera_ref);
          EdsRelease(camera_list);
          return false;
        }
      } else {
        EdsRelease(camera_list);
        return false;
      }
    } else {
      EdsRelease(camera_list);
      return false;
    }
  } else {
    return false;
  }

  EdsRelease(camera_list);
  return true;
}

#if 0
EdsError EDSCALLBACK handleObjectEvent(EdsObjectEvent event, EdsBaseRef object,
                                       EdsVoid *context) {
  EdsError err = EDS_ERR_OK;

  // Object must be released if(object)
  EdsRelease(object);
  //_syncObject->unlock();
  return err;
}

EdsError EDSCALLBACK handlePropertyEvent(EdsUInt32 inEvent,
                                         EdsUInt32 inPropertyID,
                                         EdsUInt32 inParam,
                                         EdsVoid *inContext) {
  EdsError err = EDS_ERR_OK;
  // do something
  return err;
}

EdsError EDSCALLBACK handleSateEvent(EdsStateEvent event, EdsUInt32 parameter,
                                     EdsVoid *context) {
  EdsError err = EDS_ERR_OK;
  // do something
  return err;
}
#endif

static bool press_shutter(int64_t *ts) {
  int64_t start = get_system_nanos();
  EdsError err =
      EdsSendCommand(g_state.camera, kEdsCameraCommand_PressShutterButton,
                     kEdsCameraCommand_ShutterButton_Completely_NonAF);
  int64_t delta = get_system_nanos() - start;

  if (err != EDS_ERR_OK) {
    return false;
  }

  MG_DEBUG(("Press Button: %lld ms", delta / 1000000));

  if (ts != NULL)
    *ts = start;

  return true;
}

static bool release_shutter(int64_t *ts) {
  int64_t start = get_system_nanos();
  EdsError err =
      EdsSendCommand(g_state.camera, kEdsCameraCommand_PressShutterButton,
                     kEdsCameraCommand_ShutterButton_OFF);
  int64_t end = get_system_nanos();

  if (err != EDS_ERR_OK) {
    return false;
  }

  int64_t delta = end - start;
  MG_DEBUG(("Release Button: %lld ms", delta / 1000000));

  if (ts != NULL)
    *ts = end;

  return true;
}

static void no_op_command(const struct command_t *cmd) {}

static void deinitialize_command(const struct command_t *cmd) {
  if (g_state.initialized) {
    EdsTerminateSDK();
  }
  g_state.initialized = false;
  g_state.connected = false;
}

static void initialize_command(const struct command_t *cmd) {
  if (!g_state.initialized) {
    MG_DEBUG(("Initializing"));

    if (EdsInitializeSDK() != EDS_ERR_OK) {
      MG_DEBUG(("Error initializing SDK"));
      return;
    }

    g_state.initialized = true;
  } else {
    MG_DEBUG(("Already initialized"));
  }

  MG_DEBUG(("Detecting cameras"));

  if (!detect_connected_camera()) {
    MG_DEBUG(("Error detecting camera"));
    deinitialize_command(NULL);
  }
}

static void set_bulb_mode() {
  MG_DEBUG(("Setting camera to Bulb mode"));

  EdsUInt32 shutterSpeed = 0x0c;
  EdsError err = EdsSetPropertyData(g_state.camera, kEdsPropID_Tv, 0,
                                    sizeof(EdsUInt32), &shutterSpeed);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error setting camera to Bulb mode"));
  }
}

static void lock_ui() {
  EdsError err =
      EdsSendStatusCommand(g_state.camera, kEdsCameraStatusCommand_UILock, 0);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error locking UI"));
  }
}

static void unlock_ui() {
  EdsError err =
      EdsSendStatusCommand(g_state.camera, kEdsCameraStatusCommand_UIUnLock, 0);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error unlocking UI"));
  }
}

static void connect_command(const struct command_t *cmd) {
  if (g_state.connected) {
    MG_DEBUG(("Already connected"));
    return;
  }

  MG_DEBUG(("Connecting to %s", g_state.description));

  if (EdsOpenSession(g_state.camera) == EDS_ERR_OK) {
    set_bulb_mode();
    lock_ui();
    g_state.connected = true;
  } else {
    MG_DEBUG(("Failed to connect to the camera"));
    // something bad happened, deinitialize and start again
    deinitialize_command(NULL);
  }
}

static void disconnect_command(const struct command_t *cmd) {
  if (!g_state.connected) {
    MG_DEBUG(("Already disconnected"));
    return;
  }

  unlock_ui();

  MG_DEBUG(("Disconnecting from %s", g_state.description));

  if (EdsCloseSession(g_state.camera) != EDS_ERR_OK) {
    // something bad happened, deinitialize and start again
    deinitialize_command(NULL);
  }

  g_state.connected = false;
}

static void take_picture_command(const struct command_t *cmd) {
  if (g_state.exposure == 0) {
    press_shutter(NULL);
    release_shutter(NULL);
  } else {
    int64_t exposure_ns = g_state.exposure * NANOS;
    int64_t delay_average_ns = get_delay_average();

    int64_t start_ns, end_ns;

    bool success = press_shutter(&start_ns);

    if (success)
      success = start_timer_ns(exposure_ns - delay_average_ns);

    if (success)
      success = release_shutter(&end_ns);

    if (success)
      add_delay((end_ns - start_ns) - exposure_ns);
  }

  if (++g_state.frames_taken < g_state.frames) {
    start_timer_ns(g_state.interval * NANOS);
    async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, true);
  } else {
    g_state.shooting = false;
  }
}

static void start_shooting_command(const struct command_t *cmd) {
  g_state.delay = cmd->cmd_data.start_shooting.delay;
  g_state.interval = cmd->cmd_data.start_shooting.interval;
  g_state.exposure = cmd->cmd_data.start_shooting.exposure;
  g_state.frames = cmd->cmd_data.start_shooting.frames;
  g_state.frames_taken = 0;
  g_state.shooting = true;

  if (g_state.delay > 0)
    start_timer_ns(g_state.delay * NANOS);

  async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, true);
}

static void stop_shooting_command(const struct command_t *cmd) {
  abort_timer();
  g_state.shooting = false;
}

static void terminate_command(const struct command_t *cmd) {
  g_state.running = false;
}

static const char *command_names[] = {
    "NO_OP",          "INITIALIZE",    "DEINITIALIZE",
    "CONNECT",        "DISCONNECT",    "TAKE_PICTURE",
    "START_SHOOTING", "STOP_SHOOTING", "TERMINATE",
};

typedef void (*command_handler_t)(const struct command_t *);

static const command_handler_t command_table[] = {
    [NO_OP] = no_op_command,
    [INITIALIZE] = initialize_command,
    [DEINITIALIZE] = deinitialize_command,
    [CONNECT] = connect_command,
    [DISCONNECT] = disconnect_command,
    [TAKE_PICTURE] = take_picture_command,
    [START_SHOOTING] = start_shooting_command,
    [STOP_SHOOTING] = stop_shooting_command,
    [TERMINATE] = terminate_command,
};

static void command_processor() {
  while (g_state.running) {
    struct command_t cmd = {
        .type = NO_OP,
    };

    int32_t slot = async_queue_dequeue(&g_queue, &cmd, 500 * MICROS);

    if (slot < 0) {
      EdsGetEvent();
      continue;
    }

    const char *command_name = command_names[cmd.type];
    command_handler_t handler = command_table[cmd.type];

    MG_DEBUG(("Command: %s on slot %d", command_name, slot));

    handler(&cmd);

    async_queue_unlock(&g_queue, slot);
  }
}

static size_t render_head(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<head>"
                    "  <meta name=\"viewport\" content=\"width=device-width, "
                    "initial-scale=1.0\" />"
                    "  <link rel=\"stylesheet\" href=\"index.css\">"
                    "  <script src=\"htmx.min.js\"></script>"
                    "  <script src=\"index.js\"></script>"
                    "</head>");
}

static size_t render_camera_content(void (*out)(char, void *), void *ptr,
                                    va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<div class=\"content camera\">"
                    "  <fieldset>"
                    "    <legend>Camera</legend>"
                    "    <input type=\"text\" disabled value=\"Canon\" />"
                    "  </fieldset>"
                    "  <button>Refresh</button>"
                    "</div>");
}

static size_t render_delay(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<input type=\"number\" name=\"delay\" value=\"%d\" "
                    "  inputmode=\"numeric\" "
                    "  hx-post=\"/delay\" hx-swap=\"outerHTML\" "
                    "  hx-trigger=\"keyup changed delay:500ms\" />",
                    g_state.delay);
}

static size_t render_inputs_content(void (*out)(char, void *), void *ptr,
                                    va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<div class=\"content inputs\">"
                    "  <fieldset>"
                    "    <legend>Delay (seconds)</legend>"
                    "    %M"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Exposure (seconds)</legend>"
                    "    <input type=\"number\" />"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Interval (seconds)</legend>"
                    "    <input type=\"number\" />"
                    "  </fieldset>"
                    "  <fieldset>"
                    "    <legend>Frames</legend>"
                    "    <input type=\"number\" />"
                    "  </fieldset>"
                    "</div>",
                    render_delay);
}

static size_t render_actions_content(void (*out)(char, void *), void *ptr,
                                     va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<div class=\"content actions\">"
                    "  <button>Start</button>"
                    "  <button>Stop</button>"
                    "  <button>Take Picture</button>"
                    "</div>");
}

static size_t render_content(void (*out)(char, void *), void *ptr,
                             va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<div class=\"content\">"
                    "  %M"
                    "  %M"
                    "  %M"
                    "</div>",
                    render_camera_content, render_inputs_content,
                    render_actions_content);
}

static size_t render_body(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<body>"
                    "  %M"
                    "</body>",
                    render_content);
}

static size_t render_html(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr,
                    "<!doctype html>\r\n"
                    "<html lang=\"en\">\r\n"
                    "  %M\r\n"
                    "  %M\r\n"
                    "</html>",
                    render_head, render_body);
}

static void render_index_html(struct mg_connection *c) {
  mg_http_reply(c, 200,
                "Content-Type: text/html; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n",
                "%M", render_html);
}

static void evt_handler(struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data) {
  (void)fn_data;

  if (ev == MG_EV_HTTP_MSG) {
    const struct mg_http_message *hm = (const struct mg_http_message *)ev_data;
    const struct mg_str method = hm->method;

    bool is_options = mg_vcmp(&method, "OPTIONS") == 0;

    if (is_options) {
      mg_http_reply(
          c, 204,
          "Access-Control-Allow-Origin: *\r\n"
          "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
          "Access-Control-Allow-Headers: *\r\n",
          "");
      return;
    }

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (mg_http_match_uri(hm, "/delay")) {
      struct mg_str body = hm->body;

      MG_DEBUG(("Delay = %d %.*s", body.len, body.len, body.ptr));

      char buf[32];
      if (mg_http_get_var(&body, "delay", buf, sizeof(buf)) > 0) {
        long delay = mg_json_get_long(mg_str_n(buf, 32), "$", -1);

        g_state.delay = delay;

        mg_http_reply(c, 200,
                      "Content-Type: text/html; charset=utf-8\r\n"
                      "Access-Control-Allow-Origin: *\r\n",
                      "%M", render_delay);
      } else {
        serialize_failure(c, "Some unkown error");
      }
    }

    if (is_get && mg_http_match_uri(hm, "/api/camera")) {
      async_queue_post(&g_queue, INITIALIZE, 0, NULL, false);

      if (is_initialized()) {
        serialize_camera(c);
      } else {
        serialize_failure(c, "No cameras detected");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/connect")) {
      async_queue_post(&g_queue, CONNECT, 0, NULL, false);

      if (is_connected()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Error connecting to the camera");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/disconnect")) {
      async_queue_post(&g_queue, DISCONNECT, 0, NULL, false);

      if (!is_connected()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Failed to disconnect to the camera");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/start-shoot")) {
      struct mg_str json = hm->body;

      long delay = mg_json_get_long(json, "$.delay", -1);
      long exposure = mg_json_get_long(json, "$.exposure", -1);
      long interval = mg_json_get_long(json, "$.interval", -1);
      long frames = mg_json_get_long(json, "$.frames", -1);

      if (delay < 0 || exposure < 0 || interval < 0 || frames < 0) {
        return serialize_failure(c, "Invalid arguments");
      }

      struct start_shooting_cmd cmd = {
          .delay = delay,
          .exposure = exposure,
          .interval = interval,
          .frames = frames,
      };

      async_queue_post(&g_queue, START_SHOOTING, sizeof(cmd), &cmd, false);

      if (is_shooting()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Cannot start shooting");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/stop-shoot")) {
      async_queue_post(&g_queue, STOP_SHOOTING, 0, NULL, false);

      if (!is_shooting()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Failed to stop shooting");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/take-picture")) {
      async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, false);

      serialize_success(c);
    } else if (is_get && mg_http_match_uri(hm, "/api/camera/state")) {
      serialize_state(c);
    } else if (is_get) {
      MG_DEBUG(("GET %.*s", hm->uri.len, hm->uri.ptr));

      if (mg_http_match_uri(hm, "/")) {
        render_index_html(c);
      } else {
        struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
        mg_http_serve_dir(c, ev_data, &opts);
      }
    } else {
      not_found(c);
    }
  }
}

static void *http_server_thread(void *data) {
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

static void sig_handler(int sig) {
  async_queue_post(&g_queue, DISCONNECT, 0, NULL, true);
  async_queue_post(&g_queue, TERMINATE, 0, NULL, true);
}

int main(int argc, const char *argv[]) {
  signal(SIGTERM, sig_handler);
  signal(SIGINT, sig_handler);

  fix_root_dir(argv[0]);

  main_thread = pthread_self();

#if 0
  struct sched_param param = {0};
  param.sched_priority = 99;
  pthread_setschedparam(main_thread, SCHED_FIFO, &param);
#endif

  pthread_create(&http_server, NULL, http_server_thread, NULL);

  // EDSDK demands its api call to be in the main thread on MacOS
  command_processor();

  pthread_join(http_server, NULL);

  return 0;
}
