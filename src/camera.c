// clang-format: off
#include <stdbool.h>
// clang-format: on

#include <EDSDK.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "EDSDKTypes.h"
#include "camera.h"
#include "mongoose.h"
#include "queue.h"
#include "timer.h"

// todo: use builtin camera timers

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
  return (int64_t)ts.tv_sec * SEC_TO_NS + (int64_t)ts.tv_nsec;
}

static struct camera_state_t g_state = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .running = true,
    .delay = 1,
    .exposure_ns = 5 * SEC_TO_NS,
    .interval = 1,
    .frames = 2,
    .frames_taken = 0,
    .initialized = false,
    .connected = false,
    .shooting = false,
    .description = {0},
    .camera = NULL,
};

struct sync_queue_t g_main_queue = {
    .queue = QUEUE_INITIALIZER,
    .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sync_wait = PTHREAD_COND_INITIALIZER,
    .processed = 0,
};

struct exposure_t {
  int64_t shutter_speed_ns;
  uint8_t shutter_speed;
};

static struct exposure_t g_exposures[];
static int g_exposures_size;

void get_copy_state(struct camera_state_t *state) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  memcpy(state, &g_state, sizeof(struct camera_state_t));
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

bool is_running() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.running;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
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

static void set_shutter_speed(EdsUInt32 shutter_speed) {
  MG_DEBUG(("Setting shutter speed = %x", shutter_speed));

  EdsError err = EdsSetPropertyData(g_state.camera, kEdsPropID_Tv, 0,
                                    sizeof(EdsUInt32), &shutter_speed);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error setting shutter speed"));
  }
}

static void set_bulb_mode() {
  MG_DEBUG(("Setting camera to Bulb mode"));
  set_shutter_speed(0x0C);
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
  if (!g_state.initialized || !g_state.connected)
    return;

  if (g_state.exposure_ns < g_exposures_size) {
    // using native time
    press_shutter(NULL);
  } else {
    int64_t exposure_ns = g_state.exposure_ns;
    int64_t delay_average_ns = get_delay_average();

    int64_t start_ns, end_ns;

    bool success = press_shutter(&start_ns);

    if (success) {
      g_state.shooting = start_timer_ns(exposure_ns - delay_average_ns);
    }

    success = release_shutter(&end_ns);

    if (success)
      add_delay((end_ns - start_ns) - exposure_ns);
  }

  if (g_state.shooting && ++g_state.frames_taken < g_state.frames) {
    if (start_timer_ns(g_state.interval * SEC_TO_NS)) {
      async_queue_post(&g_main_queue, TAKE_PICTURE, 0, NULL, true);
    } else {
      MG_DEBUG(("Stop shooting"));
      g_state.shooting = false;
    }
  } else {
    MG_DEBUG(("Stop shooting"));
    g_state.shooting = false;
  }
}

static void start_shooting_command(const struct command_t *cmd) {
  g_state.frames_taken = 0;
  g_state.shooting = true;

  if (g_state.exposure_ns < g_exposures_size) {
    set_shutter_speed(g_exposures[g_state.exposure_ns].shutter_speed);
  } else {
    set_bulb_mode();
  }

  if (g_state.delay > 0) {
    if (!start_timer_ns(g_state.delay * SEC_TO_NS)) {
      g_state.shooting = false;
      return;
    }
  }

  async_queue_post(&g_main_queue, TAKE_PICTURE, 0, NULL, true);
}

static void stop_shooting_command(const struct command_t *cmd) {
  // fixme: this won't work because this thread is already
  // locked on start_timer_ns()
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

static void sig_handler(int sig) {
  async_queue_post(&g_main_queue, DISCONNECT, 0, NULL, true);
  async_queue_post(&g_main_queue, TERMINATE, 0, NULL, true);
}

void command_processor() {
  signal(SIGTERM, sig_handler);
  signal(SIGINT, sig_handler);

  while (g_state.running) {
    struct command_t cmd = {
        .type = NO_OP,
    };

    int32_t slot = async_queue_dequeue(&g_main_queue, &cmd, 500 * MILLI_TO_NS);

    if (slot < 0) {
      EdsGetEvent();
      continue;
    }

    const char *command_name = command_names[cmd.type];
    command_handler_t handler = command_table[cmd.type];

    MG_DEBUG(("Command: %s on slot %d", command_name, slot));

    handler(&cmd);

    async_queue_unlock(&g_main_queue, slot);
  }
}

int64_t find_best_match(int64_t value);

void set_exposure(const char *value_str) {
  pthread_mutex_lock(&g_state.mutex);

  int32_t exposure_int = 0;
  float exposure_float = 0;

  if (sscanf(value_str, "1/%d", &exposure_int) == 1) {
    g_state.exposure_ns = SEC_TO_NS / exposure_int;
  } else if (sscanf(value_str, "%f", &exposure_float) == 1) {
    g_state.exposure_ns = exposure_float * SEC_TO_NS;
  }

  g_state.exposure_ns = find_best_match(g_state.exposure_ns);

  pthread_mutex_unlock(&g_state.mutex);
}

void set_delay(const char *value_str) {
  pthread_mutex_lock(&g_state.mutex);

  int32_t delay = 0;
  if (sscanf(value_str, "%d", &delay) == 1) {
    g_state.delay = delay;
  }

  pthread_mutex_unlock(&g_state.mutex);
}

void set_interval(const char *value_str) {
  pthread_mutex_lock(&g_state.mutex);

  int32_t interval = 0;
  if (sscanf(value_str, "%d", &interval) == 1) {
    g_state.interval = interval;
  }

  pthread_mutex_unlock(&g_state.mutex);
}

void set_frames(const char *value_str) {
  pthread_mutex_lock(&g_state.mutex);

  int32_t frames = 0;
  if (sscanf(value_str, "%d", &frames) == 1) {
    g_state.frames = frames;
  }

  pthread_mutex_unlock(&g_state.mutex);
}

void get_exposure(char *value_str, size_t size) {
  int64_t exposure = g_state.exposure_ns;

  if (exposure < g_exposures_size) {
    exposure = g_exposures[exposure].shutter_speed_ns;
  }

  // if exposure >= 300ms use decimal format
  // otherwise use fractional format
  if (exposure >= 300 * MILLI_TO_NS) {
    float seconds = exposure / (float)SEC_TO_NS;
    snprintf(value_str, size, "%.1f", seconds);
  } else {
    int seconds = SEC_TO_NS / exposure;
    snprintf(value_str, size, "1/%d", seconds);
  }
}

// todo: check if camera support all these timmings
// otherwise need to filter out the possibilities
static struct exposure_t g_exposures[] = {
    // {.shutter_speed_ns = 0xFFFFFFFFFFFFFF, .shutter_speed = 0x0C},
    {.shutter_speed_ns = 30000000000ull, .shutter_speed = 0x10},
    {.shutter_speed_ns = 25000000000ull, .shutter_speed = 0x13},
    {.shutter_speed_ns = 20000000000ull, .shutter_speed = 0x14},
    {.shutter_speed_ns = 15000000000ull, .shutter_speed = 0x18},
    {.shutter_speed_ns = 13000000000ull, .shutter_speed = 0x1B},
    {.shutter_speed_ns = 10000000000ull, .shutter_speed = 0x1C},
    {.shutter_speed_ns = 8000000000ull, .shutter_speed = 0x20},
    {.shutter_speed_ns = 6000000000ull, .shutter_speed = 0x24},
    {.shutter_speed_ns = 5000000000ull, .shutter_speed = 0x25},
    {.shutter_speed_ns = 4000000000ull, .shutter_speed = 0x28},
    {.shutter_speed_ns = 3200000000ull, .shutter_speed = 0x2B},
    {.shutter_speed_ns = 3000000000ull, .shutter_speed = 0x2C},
    {.shutter_speed_ns = 2500000000ull, .shutter_speed = 0x2D},
    {.shutter_speed_ns = 2000000000ull, .shutter_speed = 0x30},
    {.shutter_speed_ns = 1600000000ull, .shutter_speed = 0x33},
    {.shutter_speed_ns = 1500000000ull, .shutter_speed = 0x34},
    {.shutter_speed_ns = 1300000000ull, .shutter_speed = 0x35},
    {.shutter_speed_ns = 1000000000ull, .shutter_speed = 0x38},
    {.shutter_speed_ns = 800000000ull, .shutter_speed = 0x3B},
    {.shutter_speed_ns = 700000000ull, .shutter_speed = 0x3C},
    {.shutter_speed_ns = 600000000ull, .shutter_speed = 0x3D},
    {.shutter_speed_ns = 500000000ull, .shutter_speed = 0x40},
    {.shutter_speed_ns = 400000000ull, .shutter_speed = 0x43},
    {.shutter_speed_ns = 300000000ull, .shutter_speed = 0x44},
    {.shutter_speed_ns = SEC_TO_NS / 4, .shutter_speed = 0x48},
    {.shutter_speed_ns = SEC_TO_NS / 5, .shutter_speed = 0x4B},
    {.shutter_speed_ns = SEC_TO_NS / 6, .shutter_speed = 0x4C},
    {.shutter_speed_ns = SEC_TO_NS / 8, .shutter_speed = 0x50},
    {.shutter_speed_ns = SEC_TO_NS / 10, .shutter_speed = 0x54},
    {.shutter_speed_ns = SEC_TO_NS / 13, .shutter_speed = 0x55},
    {.shutter_speed_ns = SEC_TO_NS / 15, .shutter_speed = 0x58},
    {.shutter_speed_ns = SEC_TO_NS / 20, .shutter_speed = 0x5C},
    {.shutter_speed_ns = SEC_TO_NS / 25, .shutter_speed = 0x5D},
    {.shutter_speed_ns = SEC_TO_NS / 30, .shutter_speed = 0x60},
    {.shutter_speed_ns = SEC_TO_NS / 40, .shutter_speed = 0x63},
    {.shutter_speed_ns = SEC_TO_NS / 45, .shutter_speed = 0x64},
    {.shutter_speed_ns = SEC_TO_NS / 50, .shutter_speed = 0x65},
    {.shutter_speed_ns = SEC_TO_NS / 60, .shutter_speed = 0x68},
    {.shutter_speed_ns = SEC_TO_NS / 80, .shutter_speed = 0x6B},
    {.shutter_speed_ns = SEC_TO_NS / 90, .shutter_speed = 0x6C},
    {.shutter_speed_ns = SEC_TO_NS / 100, .shutter_speed = 0x6D},
    {.shutter_speed_ns = SEC_TO_NS / 125, .shutter_speed = 0x70},
    {.shutter_speed_ns = SEC_TO_NS / 160, .shutter_speed = 0x73},
    {.shutter_speed_ns = SEC_TO_NS / 180, .shutter_speed = 0x74},
    {.shutter_speed_ns = SEC_TO_NS / 200, .shutter_speed = 0x75},
    {.shutter_speed_ns = SEC_TO_NS / 250, .shutter_speed = 0x78},
    {.shutter_speed_ns = SEC_TO_NS / 320, .shutter_speed = 0x7B},
    {.shutter_speed_ns = SEC_TO_NS / 350, .shutter_speed = 0x7C},
    {.shutter_speed_ns = SEC_TO_NS / 400, .shutter_speed = 0x7D},
    {.shutter_speed_ns = SEC_TO_NS / 500, .shutter_speed = 0x80},
    {.shutter_speed_ns = SEC_TO_NS / 640, .shutter_speed = 0x83},
    {.shutter_speed_ns = SEC_TO_NS / 750, .shutter_speed = 0x84},
    {.shutter_speed_ns = SEC_TO_NS / 800, .shutter_speed = 0x85},
    {.shutter_speed_ns = SEC_TO_NS / 1000, .shutter_speed = 0x88},
    {.shutter_speed_ns = SEC_TO_NS / 1250, .shutter_speed = 0x8B},
    {.shutter_speed_ns = SEC_TO_NS / 1500, .shutter_speed = 0x8C},
    {.shutter_speed_ns = SEC_TO_NS / 1600, .shutter_speed = 0x8D},
    {.shutter_speed_ns = SEC_TO_NS / 2000, .shutter_speed = 0x90},
    {.shutter_speed_ns = SEC_TO_NS / 2500, .shutter_speed = 0x93},
    {.shutter_speed_ns = SEC_TO_NS / 3000, .shutter_speed = 0x94},
    {.shutter_speed_ns = SEC_TO_NS / 3200, .shutter_speed = 0x95},
    {.shutter_speed_ns = SEC_TO_NS / 4000, .shutter_speed = 0x98},
    {.shutter_speed_ns = SEC_TO_NS / 5000, .shutter_speed = 0x9B},
    {.shutter_speed_ns = SEC_TO_NS / 6000, .shutter_speed = 0x9C},
    {.shutter_speed_ns = SEC_TO_NS / 6400, .shutter_speed = 0x9D},
    {.shutter_speed_ns = SEC_TO_NS / 8000, .shutter_speed = 0xA0},
    {.shutter_speed_ns = SEC_TO_NS / 10000, .shutter_speed = 0xA3},
    {.shutter_speed_ns = SEC_TO_NS / 12800, .shutter_speed = 0xA5},
    {.shutter_speed_ns = SEC_TO_NS / 16000, .shutter_speed = 0xA8},
};

static int g_exposures_size = sizeof(g_exposures) / sizeof(struct exposure_t);

int64_t find_best_match(int64_t value) {
  int best_match = -1;
  int64_t min_diff = 0x1fffffffffffffff;

  for (int i = g_exposures_size - 1; i >= 0; i--) {
    int64_t shutter_speed = g_exposures[i].shutter_speed_ns;
    int64_t diff = llabs(value - shutter_speed);

    if (diff == 0)
      return i;

    if (diff < min_diff) {
      best_match = i;
      min_diff = diff;
    }
  }

  return best_match >= 0 ? best_match : value;
}
