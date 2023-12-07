#include <EDSDK.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "mongoose.h"

#define NANOS (MICROS * 1000ll)
#define MICROS (MILLIS * 1000ll)
#define MILLIS (1000ll)

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

static bool g_running = true;

static struct {
  pthread_mutex_t mutex;
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

enum command_type {
  NO_OP,
  INITIALIZE,
  DEINITIALIZE,
  CONNECT,
  DISCONNECT,
  TAKE_PICTURE,
  START_SHOOTING,
  STOP_SHOOTING,
  TERMINATE,
};

struct start_shooting_cmd {
  long delay;
  long exposure;
  long interval;
  long frames;
};

struct command_t {
  enum command_type type;
  union {
    struct start_shooting_cmd start_shooting;
  } cmd_data;
};

#define DELAYS_SIZE 32

static struct {
  bool aborted;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int64_t delays[DELAYS_SIZE];
  int delays_start;
  int delays_length;
} g_timer = {
    .aborted = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .delays = {0},
    .delays_start = 0,
    .delays_length = 0,
};

static void add_delay(int64_t delay) {
  g_timer.delays[(g_timer.delays_start + g_timer.delays_length) % DELAYS_SIZE] =
      delay;

  if (g_timer.delays_length < DELAYS_SIZE) {
    g_timer.delays_length++;
  } else {
    g_timer.delays_start = (g_timer.delays_start + 1) % DELAYS_SIZE;
  }
}

static int64_t get_delay_average() {
  if (g_timer.delays_length == 0)
    return 0;

  int64_t sum = 0;

  for (int i = 0; i < g_timer.delays_length; i++)
    sum += g_timer.delays[(g_timer.delays_start + i) % DELAYS_SIZE];

  return sum / g_timer.delays_length;
}

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

static void adjust_timer_ns(struct timespec *ts, int64_t timer_ns) {
  clock_gettime(CLOCK_REALTIME, ts);

  ts->tv_sec += timer_ns / NANOS;
  ts->tv_nsec += timer_ns % NANOS;

  ts->tv_sec += ts->tv_nsec / NANOS;
  ts->tv_nsec = ts->tv_nsec % NANOS;
}

static bool start_timer_ns(int64_t timer_ns) {
  MG_DEBUG(("Timer %lld", timer_ns));

  struct timespec ts = {0, 0};
  adjust_timer_ns(&ts, timer_ns);

  assert(pthread_mutex_lock(&g_timer.mutex) == 0);

  {
    g_timer.aborted = false;
    int ret = pthread_cond_timedwait(&g_timer.cond, &g_timer.mutex, &ts);
    assert(ret != EINVAL);
    // 0 means cond was triggered, otherwise ETIMEDOUT is returned
    g_timer.aborted = ret == 0;
  }

  bool aborted = g_timer.aborted;
  assert(pthread_mutex_unlock(&g_timer.mutex) == 0);
  return !aborted;
}

static void abort_timer() {
  // at this point timer_mutex is unlock wating for the condition
  assert(pthread_mutex_lock(&g_timer.mutex) == 0);
  if (!g_timer.aborted)
    pthread_cond_broadcast(&g_timer.cond);
  g_timer.aborted = true;
  assert(pthread_mutex_unlock(&g_timer.mutex) == 0);
}

#define BUFFER_SIZE 8

struct thread_queue_t {
  struct command_t buffer[BUFFER_SIZE];
  int size;
  int nextin;
  int nextout;
  pthread_mutex_t mutex;
  pthread_cond_t produced;
  pthread_cond_t consumed;
};

int enqueue_command(struct thread_queue_t *b, const struct command_t *cmd) {
  assert(pthread_mutex_lock(&b->mutex) == 0);

  while (b->size >= BUFFER_SIZE)
    assert(pthread_cond_wait(&b->consumed, &b->mutex) == 0);

  assert(b->size < BUFFER_SIZE);

  int nextin = b->nextin++;
  b->nextin %= BUFFER_SIZE;
  b->size++;

  b->buffer[nextin] = *cmd;

  assert(pthread_cond_signal(&b->produced) == 0);
  assert(pthread_mutex_unlock(&b->mutex) == 0);

  return nextin;
}

int dequeue_command(struct thread_queue_t *b, struct command_t *cmd,
                    int64_t timer_ns) {
  assert(pthread_mutex_lock(&b->mutex) == 0);

  struct timespec ts = {0, 0};
  adjust_timer_ns(&ts, timer_ns);

  while (b->size <= 0) {
    int ret = pthread_cond_timedwait(&b->produced, &b->mutex, &ts);
    if (ret == ETIMEDOUT) {
      assert(pthread_mutex_unlock(&b->mutex) == 0);
      return -1;
    }
  }

  assert(b->size > 0);

  int nextout = b->nextout++;
  b->nextout %= BUFFER_SIZE;
  b->size--;

  *cmd = b->buffer[nextout];

  assert(pthread_cond_signal(&b->consumed) == 0);
  assert(pthread_mutex_unlock(&b->mutex) == 0);

  return nextout;
}

struct sync_queue_t {
  struct thread_queue_t queue;
  pthread_mutex_t sync_mutex;
  pthread_cond_t sync_wait;
  int32_t processed;
};

static struct sync_queue_t g_queue = {
    .queue =
        {
            .buffer = {0},
            .size = 0,
            .nextin = 0,
            .nextout = 0,
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .produced = PTHREAD_COND_INITIALIZER,
            .consumed = PTHREAD_COND_INITIALIZER,
        },
    .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sync_wait = PTHREAD_COND_INITIALIZER,
    .processed = 0,
};

static void notify_processed(int slot) {
  assert(pthread_mutex_lock(&g_queue.sync_mutex) == 0);
  g_queue.processed |= (1 << slot);
  assert(pthread_cond_signal(&g_queue.sync_wait) == 0);
  assert(pthread_mutex_unlock(&g_queue.sync_mutex) == 0);
}

static void post_command(enum command_type type, size_t cmd_size,
                         const void *cmd_data, bool wait) {
  assert(pthread_mutex_lock(&g_queue.sync_mutex) == 0);

  struct command_t cmd = {
      .type = type,
  };

  if (cmd_data != NULL)
    memcpy(&cmd.cmd_data, cmd_data, cmd_size);

  int slot = enqueue_command(&g_queue.queue, &cmd);
  int mask = 1 << slot;

  g_queue.processed &= ~mask;

  while (wait && (g_queue.processed & mask) == 0) {
    assert(pthread_cond_wait(&g_queue.sync_wait, &g_queue.sync_mutex) == 0);
  }

  assert(pthread_mutex_unlock(&g_queue.sync_mutex) == 0);
}

static void post_command_sync(enum command_type type, size_t cmd_size,
                              const void *cmd_data) {
  post_command(type, cmd_size, cmd_data, true);
}

static void post_command_async(enum command_type type, size_t cmd_size,
                               const void *cmd_data) {
  post_command(type, cmd_size, cmd_data, false);
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
    post_command_async(TAKE_PICTURE, 0, NULL);
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

  post_command_async(TAKE_PICTURE, 0, NULL);
}

static void stop_shooting_command(const struct command_t *cmd) {
  abort_timer();
  g_state.shooting = false;
}

static void terminate_command(const struct command_t *cmd) {
  g_running = false;
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

static void shooting_state_machine() {
  while (g_running) {
    struct command_t cmd = {
        .type = NO_OP,
    };

    int slot = dequeue_command(&g_queue.queue, &cmd, 500 * MICROS);

    if (slot < 0) {
      EdsGetEvent();
      continue;
    }

    const char *command_name = command_names[cmd.type];
    command_handler_t handler = command_table[cmd.type];

    MG_DEBUG(("Command: %s on slot %d", command_name, slot));

    handler(&cmd);

    notify_processed(slot);
  }
}

static void evt_handler(struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data) {
  (void)fn_data;

  if (ev == MG_EV_POLL) {
  }

  if (ev == MG_EV_HTTP_MSG) {
    const struct mg_http_message *hm = (const struct mg_http_message *)ev_data;
    const struct mg_str method = hm->method;

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (is_get && mg_http_match_uri(hm, "/api/camera")) {
      post_command_sync(INITIALIZE, 0, NULL);

      if (is_initialized()) {
        serialize_camera(c);
      } else {
        serialize_failure(c, "No cameras detected");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/connect")) {
      post_command_sync(CONNECT, 0, NULL);

      if (is_connected()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Error connecting to the camera");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/disconnect")) {
      post_command_sync(DISCONNECT, 0, NULL);

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

      post_command_sync(START_SHOOTING, sizeof(cmd), &cmd);

      if (is_shooting()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Cannot start shooting");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/stop-shoot")) {
      post_command_sync(STOP_SHOOTING, 0, NULL);

      if (!is_shooting()) {
        serialize_state(c);
      } else {
        serialize_failure(c, "Failed to stop shooting");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/take-picture")) {
      post_command_sync(TAKE_PICTURE, 0, NULL);

      serialize_success(c);
    } else if (is_get && mg_http_match_uri(hm, "/api/camera/state")) {
      serialize_state(c);
    } else if (is_get) {
      struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
      mg_http_serve_dir(c, ev_data, &opts);
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
  while (g_running) {
    mg_mgr_poll(&mgr, 1000);
  }
  mg_mgr_free(&mgr);

  return NULL;
}

static void sig_handler(int sig) {
  post_command_async(DISCONNECT, 0, NULL);
  post_command_async(TERMINATE, 0, NULL);
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
  shooting_state_machine();

  pthread_join(http_server, NULL);

  return 0;
}
