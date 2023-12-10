// clang-format: off
#include <stdbool.h>
// clang-format: on

#include <EDSDK.h>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

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
  return (int64_t)ts.tv_sec * NANOS + (int64_t)ts.tv_nsec;
}

struct camera_state_t g_state = {
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

struct sync_queue_t g_queue = {
    .queue = QUEUE_INITIALIZER,
    .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sync_wait = PTHREAD_COND_INITIALIZER,
    .processed = 0,
};

bool is_initialized() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.initialized;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

bool is_connected() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.connected;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

bool is_shooting() {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.shooting;
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
  if (!g_state.initialized || !g_state.connected)
    return;

  if (g_state.exposure == 0) {
    press_shutter(NULL);
    release_shutter(NULL);
  } else {
    int64_t exposure_ns = g_state.exposure * NANOS;
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
    if (start_timer_ns(g_state.interval * NANOS)) {
      async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, true);
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

  if (g_state.delay > 0) {
    if (!start_timer_ns(g_state.delay * NANOS)) {
      g_state.shooting = false;
      return;
    }
  }

  async_queue_post(&g_queue, TAKE_PICTURE, 0, NULL, true);
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

void command_processor() {
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

