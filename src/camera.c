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
    .exposure_ns = 5 * NANOS,
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

  if (g_state.exposure_ns == 0) {
    press_shutter(NULL);
    release_shutter(NULL);
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

/*
0x0C Bulb
0x10 30"
0x13 25"
0x14 20"
0x18 15"
0x1B 13"
0x1C 10"
0x20 8"
0x24 6"
0x25 5"
0x28 4"
0x2B 3"2
0x2C 3"
0x2D 2"5
0x30 2"
0x33 1"6
0x34 1"5
0x35 1"3
0x38 1"
0x3B 0"8
0x3C 0"7
0x3D 0"6
0x40 0"5
0x43 0"4
0x44 0"3

0x48 1/4
0x4B 1/5
0x4C 1/6
0x50 1/8
0x54 1/10
0x55 1/13
0x58 1/15
0x5C 1/20
0x5D 1/25
0x60 1/30
0x63 1/40
0x64 1/45
0x65 1/50
0x68 1/60
0x6B 1/80
0x6C 1/90
0x6D 1/100
0x70 1/125
0x73 1/160
0x74 1/180
0x75 1/200
0x78 1/250
0x7B 1/320
0x7C 1/350
0x7D 1/400
0x80 1/500
0x83 1/640
0x84 1/750
0x85 1/800
0x88 1/1000
0x8B 1/1250
0x8C 1/1500
0x8D 1/1600
0x90 1/2000
0x93 1/2500
0x94 1/3000
0x95 1/3200
0x98 1/4000
0x9B 1/5000
0x9C 1/6000
0x9D 1/6400
0xA0 1/8000
0xA3 1/10000
0xA5 1/12800
0xA8 1/16000
*/
