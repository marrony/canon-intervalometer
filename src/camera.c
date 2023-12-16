// clang-format: off
#include <stdbool.h>
// clang-format: on

#include <EDSDK.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "EDSDKErrors.h"
#include "EDSDKTypes.h"
#include "camera.h"
#include "mongoose.h"
#include "queue.h"
#include "timer.h"

static void fill_exposures(void);
static void fill_iso_speeds(void);
static void copy_all_exposures(void);
static void copy_all_isos(void);

#ifdef __MACOS__
extern __uint64_t __thread_selfid(void);
#endif

static int64_t get_system_nanos(void) {
  struct timespec ts = {0, 0};
  timespec_get(&ts, TIME_UTC);
  return (int64_t)ts.tv_sec * SEC_TO_NS + (int64_t)ts.tv_nsec;
}

static struct {
  pthread_mutex_t mutex;
  EdsCameraRef camera;
  struct camera_state_t state;
} g_state = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .camera = NULL,
    .state =
        {
            .running = true,
            .delay_ns = 1 * SEC_TO_NS,
            .iso_index = 0,
            .exposure_index = 0,
            .exposure_ns = 31 * SEC_TO_NS,
            .interval_ns = 1 * SEC_TO_NS,
            .frames = 2,
            .frames_taken = 0,
            .initialized = false,
            .connected = false,
            .shooting = false,
            .description = {0},
        },
};

struct sync_queue_t g_main_queue = {
    .queue = QUEUE_INITIALIZER,
    .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
    .sync_wait = PTHREAD_COND_INITIALIZER,
    .processed = 0,
};

struct exposure_t {
  int64_t shutter_speed_ns;
  EdsUInt32 shutter_param;
};

struct iso_t {
  const char *iso_description;
  EdsUInt32 iso_param;
};

static const struct exposure_t g_all_exposures[] = {
    // {.shutter_speed_ns = 0xFFFFFFFFFFFFFF, .shutter_speed = 0x0C},
    {.shutter_speed_ns = 30000000000ull, .shutter_param = 0x10},
    {.shutter_speed_ns = 25000000000ull, .shutter_param = 0x13},
    {.shutter_speed_ns = 20000000000ull, .shutter_param = 0x14},
    {.shutter_speed_ns = 15000000000ull, .shutter_param = 0x18},
    {.shutter_speed_ns = 13000000000ull, .shutter_param = 0x1B},
    {.shutter_speed_ns = 10000000000ull, .shutter_param = 0x1C},
    {.shutter_speed_ns = 8000000000ull, .shutter_param = 0x20},
    {.shutter_speed_ns = 6000000000ull, .shutter_param = 0x24},
    {.shutter_speed_ns = 5000000000ull, .shutter_param = 0x25},
    {.shutter_speed_ns = 4000000000ull, .shutter_param = 0x28},
    {.shutter_speed_ns = 3200000000ull, .shutter_param = 0x2B},
    {.shutter_speed_ns = 3000000000ull, .shutter_param = 0x2C},
    {.shutter_speed_ns = 2500000000ull, .shutter_param = 0x2D},
    {.shutter_speed_ns = 2000000000ull, .shutter_param = 0x30},
    {.shutter_speed_ns = 1600000000ull, .shutter_param = 0x33},
    {.shutter_speed_ns = 1500000000ull, .shutter_param = 0x34},
    {.shutter_speed_ns = 1300000000ull, .shutter_param = 0x35},
    {.shutter_speed_ns = 1000000000ull, .shutter_param = 0x38},
    {.shutter_speed_ns = 800000000ull, .shutter_param = 0x3B},
    {.shutter_speed_ns = 700000000ull, .shutter_param = 0x3C},
    {.shutter_speed_ns = 600000000ull, .shutter_param = 0x3D},
    {.shutter_speed_ns = 500000000ull, .shutter_param = 0x40},
    {.shutter_speed_ns = 400000000ull, .shutter_param = 0x43},
    {.shutter_speed_ns = 300000000ull, .shutter_param = 0x44},
    {.shutter_speed_ns = SEC_TO_NS / 4, .shutter_param = 0x48},
    {.shutter_speed_ns = SEC_TO_NS / 5, .shutter_param = 0x4B},
    {.shutter_speed_ns = SEC_TO_NS / 6, .shutter_param = 0x4C},
    {.shutter_speed_ns = SEC_TO_NS / 8, .shutter_param = 0x50},
    {.shutter_speed_ns = SEC_TO_NS / 10, .shutter_param = 0x54},
    {.shutter_speed_ns = SEC_TO_NS / 13, .shutter_param = 0x55},
    {.shutter_speed_ns = SEC_TO_NS / 15, .shutter_param = 0x58},
    {.shutter_speed_ns = SEC_TO_NS / 20, .shutter_param = 0x5C},
    {.shutter_speed_ns = SEC_TO_NS / 25, .shutter_param = 0x5D},
    {.shutter_speed_ns = SEC_TO_NS / 30, .shutter_param = 0x60},
    {.shutter_speed_ns = SEC_TO_NS / 40, .shutter_param = 0x63},
    {.shutter_speed_ns = SEC_TO_NS / 45, .shutter_param = 0x64},
    {.shutter_speed_ns = SEC_TO_NS / 50, .shutter_param = 0x65},
    {.shutter_speed_ns = SEC_TO_NS / 60, .shutter_param = 0x68},
    {.shutter_speed_ns = SEC_TO_NS / 80, .shutter_param = 0x6B},
    {.shutter_speed_ns = SEC_TO_NS / 90, .shutter_param = 0x6C},
    {.shutter_speed_ns = SEC_TO_NS / 100, .shutter_param = 0x6D},
    {.shutter_speed_ns = SEC_TO_NS / 125, .shutter_param = 0x70},
    {.shutter_speed_ns = SEC_TO_NS / 160, .shutter_param = 0x73},
    {.shutter_speed_ns = SEC_TO_NS / 180, .shutter_param = 0x74},
    {.shutter_speed_ns = SEC_TO_NS / 200, .shutter_param = 0x75},
    {.shutter_speed_ns = SEC_TO_NS / 250, .shutter_param = 0x78},
    {.shutter_speed_ns = SEC_TO_NS / 320, .shutter_param = 0x7B},
    {.shutter_speed_ns = SEC_TO_NS / 350, .shutter_param = 0x7C},
    {.shutter_speed_ns = SEC_TO_NS / 400, .shutter_param = 0x7D},
    {.shutter_speed_ns = SEC_TO_NS / 500, .shutter_param = 0x80},
    {.shutter_speed_ns = SEC_TO_NS / 640, .shutter_param = 0x83},
    {.shutter_speed_ns = SEC_TO_NS / 750, .shutter_param = 0x84},
    {.shutter_speed_ns = SEC_TO_NS / 800, .shutter_param = 0x85},
    {.shutter_speed_ns = SEC_TO_NS / 1000, .shutter_param = 0x88},
    {.shutter_speed_ns = SEC_TO_NS / 1250, .shutter_param = 0x8B},
    {.shutter_speed_ns = SEC_TO_NS / 1500, .shutter_param = 0x8C},
    {.shutter_speed_ns = SEC_TO_NS / 1600, .shutter_param = 0x8D},
    {.shutter_speed_ns = SEC_TO_NS / 2000, .shutter_param = 0x90},
    {.shutter_speed_ns = SEC_TO_NS / 2500, .shutter_param = 0x93},
    {.shutter_speed_ns = SEC_TO_NS / 3000, .shutter_param = 0x94},
    {.shutter_speed_ns = SEC_TO_NS / 3200, .shutter_param = 0x95},
    {.shutter_speed_ns = SEC_TO_NS / 4000, .shutter_param = 0x98},
    {.shutter_speed_ns = SEC_TO_NS / 5000, .shutter_param = 0x9B},
    {.shutter_speed_ns = SEC_TO_NS / 6000, .shutter_param = 0x9C},
    {.shutter_speed_ns = SEC_TO_NS / 6400, .shutter_param = 0x9D},
    {.shutter_speed_ns = SEC_TO_NS / 8000, .shutter_param = 0xA0},
    {.shutter_speed_ns = SEC_TO_NS / 10000, .shutter_param = 0xA3},
    {.shutter_speed_ns = SEC_TO_NS / 12800, .shutter_param = 0xA5},
    {.shutter_speed_ns = SEC_TO_NS / 16000, .shutter_param = 0xA8},
};

static const struct iso_t g_all_isos[] = {
    {.iso_description = "Auto", .iso_param = 0x0},
    {.iso_description = "ISO 6", .iso_param = 0x28},
    {.iso_description = "ISO 12", .iso_param = 0x30},
    {.iso_description = "ISO 25", .iso_param = 0x38},
    {.iso_description = "ISO 50", .iso_param = 0x40},
    {.iso_description = "ISO 100", .iso_param = 0x48},
    {.iso_description = "ISO 125", .iso_param = 0x4b},
    {.iso_description = "ISO 160", .iso_param = 0x4d},
    {.iso_description = "ISO 200", .iso_param = 0x50},
    {.iso_description = "ISO 250", .iso_param = 0x53},
    {.iso_description = "ISO 320", .iso_param = 0x55},
    {.iso_description = "ISO 400", .iso_param = 0x56},
    {.iso_description = "ISO 500", .iso_param = 0x5b},
    {.iso_description = "ISO 640", .iso_param = 0x5d},
    {.iso_description = "ISO 800", .iso_param = 0x60},
    {.iso_description = "ISO 1000", .iso_param = 0x63},
    {.iso_description = "ISO 1250", .iso_param = 0x65},
    {.iso_description = "ISO 1600", .iso_param = 0x68},
    {.iso_description = "ISO 2000", .iso_param = 0x6b},
    {.iso_description = "ISO 2500", .iso_param = 0x6d},
    {.iso_description = "ISO 3200", .iso_param = 0x70},
    {.iso_description = "ISO 4000", .iso_param = 0x73},
    {.iso_description = "ISO 5000", .iso_param = 0x75},
    {.iso_description = "ISO 6400", .iso_param = 0x78},
    {.iso_description = "ISO 8000", .iso_param = 0x07b},
    {.iso_description = "ISO 10000", .iso_param = 0x7d},
    {.iso_description = "ISO 12800", .iso_param = 0x80},
    {.iso_description = "ISO 16000", .iso_param = 0x83},
    {.iso_description = "ISO 20000", .iso_param = 0x85},
    {.iso_description = "ISO 25600", .iso_param = 0x88},
    {.iso_description = "ISO 32000", .iso_param = 0x8b},
    {.iso_description = "ISO 40000", .iso_param = 0x8d},
    {.iso_description = "ISO 51200", .iso_param = 0x90},
    {.iso_description = "ISO 64000", .iso_param = 0x3},
    {.iso_description = "ISO 80000", .iso_param = 0x95},
    {.iso_description = "ISO 102400", .iso_param = 0x98},
    {.iso_description = "ISO 204800", .iso_param = 0xa0},
    {.iso_description = "ISO 409600", .iso_param = 0xa8},
    {.iso_description = "ISO 819200", .iso_param = 0xb0},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define ALL_EXPOSURES_SIZE ARRAY_SIZE(g_all_exposures)
#define ALL_ISOS_SIZE ARRAY_SIZE(g_all_isos)

static struct exposure_t g_exposures[ALL_EXPOSURES_SIZE] = {0};
static int g_exposures_size = 0;

static struct iso_t g_isos[ALL_ISOS_SIZE] = {0};
static int g_isos_size = 0;

void get_copy_state(struct camera_state_t *state) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  memcpy(state, &g_state.state, sizeof(struct camera_state_t));
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

bool is_running(void) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);
  bool ret = g_state.state.running;
  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
  return ret;
}

static int nssleep(int64_t timer_ns) {
  struct timespec ts = {
      .tv_sec = timer_ns / SEC_TO_NS,
      .tv_nsec = timer_ns % SEC_TO_NS,
  };
  struct timespec rem = {0, 0};

  while (nanosleep(&ts, &rem) < 0) {
    if (errno != EINTR)
      return -1;
    ts = rem;
  }

  return 0;
}

static EdsError EDSCALLBACK handle_object_event(EdsObjectEvent event,
                                                EdsBaseRef object_ref,
                                                EdsVoid *data) {
  MG_DEBUG(("Event = %u", event));
  return EdsRelease(object_ref);
}

static EdsError EDSCALLBACK handle_property_event(EdsPropertyEvent event,
                                                  EdsUInt32 property_id,
                                                  EdsUInt32 param,
                                                  EdsVoid *data) {
  MG_DEBUG(
      ("Event = %u, Property = %u, Param = %u", event, property_id, param));
  return EDS_ERR_OK;
}

static EdsError EDSCALLBACK handle_state_event(EdsStateEvent event,
                                               EdsUInt32 param, EdsVoid *data) {
  MG_DEBUG(("Event = %u, Param = %u", event, param));
  return EDS_ERR_OK;
}

static void attach_camera_callbacks(void) {
  EdsSetObjectEventHandler(g_state.camera, kEdsObjectEvent_All,
                           handle_object_event, NULL);
  EdsSetPropertyEventHandler(g_state.camera, kEdsPropertyEvent_All,
                             handle_property_event, NULL);
  EdsSetCameraStateEventHandler(g_state.camera, kEdsStateEvent_All,
                                handle_state_event, NULL);
}

static bool detect_connected_camera(void) {
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
          strncpy(g_state.state.description, device_info.szDeviceDescription,
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

static bool press_shutter(int64_t *ts) {
  int64_t start = get_system_nanos();
  EdsError err =
      EdsSendCommand(g_state.camera, kEdsCameraCommand_PressShutterButton,
                     kEdsCameraCommand_ShutterButton_Completely_NonAF);
  int64_t delta = get_system_nanos() - start;

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Press Shutter err = %d", err));
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
    MG_DEBUG(("Release Shutter err = %d", err));
    return false;
  }

  int64_t delta = end - start;
  MG_DEBUG(("Release Button: %lld ms", delta / 1000000));

  if (ts != NULL)
    *ts = end;

  return true;
}

static void no_op_command(void *data) {}

static void deinitialize_command(void *data) {
  if (g_state.camera != NULL) {
    EdsRelease(g_state.camera);
    g_state.camera = NULL;
  }

  if (g_state.state.initialized) {
    EdsTerminateSDK();
  }
  g_state.state.initialized = false;
  g_state.state.connected = false;
}

static void initialize_command(void *data) {
  if (!g_state.state.initialized) {
    MG_DEBUG(("Initializing"));

    if (EdsInitializeSDK() != EDS_ERR_OK) {
      MG_DEBUG(("Error initializing SDK"));
      return;
    }

    g_state.state.initialized = true;
  } else {
    MG_DEBUG(("Already initialized"));
  }

  MG_DEBUG(("Detecting cameras"));

  if (!detect_connected_camera()) {
    MG_DEBUG(("Error detecting camera"));
    deinitialize_command(NULL);
  }

  attach_camera_callbacks();
}

static void set_shutter_speed(EdsUInt32 shutter_speed) {
  MG_DEBUG(("Setting shutter speed = %x", shutter_speed));

  EdsError err = EdsSetPropertyData(g_state.camera, kEdsPropID_Tv, 0,
                                    sizeof(EdsUInt32), &shutter_speed);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error setting shutter speed"));
  }
}

static void set_iso_speed(EdsUInt32 iso_speed) {
  MG_DEBUG(("Setting iso speed = %x", iso_speed));

  EdsError err = EdsSetPropertyData(g_state.camera, kEdsPropID_ISOSpeed, 0,
                                    sizeof(EdsUInt32), &iso_speed);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error setting iso speed"));
  }
}

static void update_shutter_speed(void) {
  if (!g_state.state.initialized || !g_state.state.connected) {
    return;
  }

  if (g_state.state.exposure_index < g_exposures_size) {
    set_shutter_speed(g_exposures[g_state.state.exposure_index].shutter_param);
  } else {
    MG_DEBUG(("Setting camera to Bulb mode"));
    set_shutter_speed(0x0C);
  }
}

static void update_iso_speed(void) {
  if (!g_state.state.initialized || !g_state.state.connected) {
    return;
  }

  if (g_state.state.iso_index < g_isos_size) {
    set_iso_speed(g_isos[g_state.state.iso_index].iso_param);
  } else {
    MG_DEBUG(("Setting camera to ISO auto"));
    set_iso_speed(0x0);
  }
}

static void lock_ui(void) {
  EdsError err =
      EdsSendStatusCommand(g_state.camera, kEdsCameraStatusCommand_UILock, 0);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error locking UI"));
  }
}

static void unlock_ui(void) {
  EdsError err =
      EdsSendStatusCommand(g_state.camera, kEdsCameraStatusCommand_UIUnLock, 0);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error unlocking UI"));
  }
}

static void connect_command(void *data) {
  if (g_state.state.connected) {
    MG_DEBUG(("Already connected"));
    return;
  }

  MG_DEBUG(("Connecting to %s", g_state.state.description));

  if (EdsOpenSession(g_state.camera) == EDS_ERR_OK) {
    g_state.state.connected = true;
    fill_exposures();
    fill_iso_speeds();
    lock_ui();
    update_shutter_speed();
    update_iso_speed();
  } else {
    MG_DEBUG(("Failed to connect to the camera"));
    // something bad happened, deinitialize and start again
    deinitialize_command(NULL);
  }
}

static void disconnect_command(void *data) {
  if (!g_state.state.connected) {
    MG_DEBUG(("Already disconnected"));
    return;
  }

  unlock_ui();

  MG_DEBUG(("Disconnecting from %s", g_state.state.description));

  if (EdsCloseSession(g_state.camera) != EDS_ERR_OK) {
    // something bad happened, deinitialize and start again
    deinitialize_command(NULL);
  }

  g_state.state.connected = false;
}

static void initial_delay_command(void *data) {
  if (g_state.state.delay_ns > 0) {
    if (!nssleep(g_state.state.delay_ns)) {
      g_state.state.shooting = false;
      return;
    }
  }

  async_queue_post(&g_main_queue, TAKE_PICTURE, NULL, /*async*/ true);
}

static void interval_delay_command(void *data) {
  if (nssleep(g_state.state.interval_ns) == 0) {
    async_queue_post(&g_main_queue, TAKE_PICTURE, NULL, /*async*/ true);
  } else {
    MG_DEBUG(("Stop shooting"));
    g_state.state.shooting = false;
  }
}

static void take_picture_command(void *data) {
  if (!g_state.state.initialized || !g_state.state.connected)
    return;

  if (g_state.state.exposure_index < g_exposures_size) {
    // using native time
    press_shutter(NULL);
    release_shutter(NULL);
  } else {
    int64_t delay_average_ns = get_delay_average();

    int64_t start_ns, end_ns;

    bool success = press_shutter(&start_ns);

    if (success) {
      g_state.state.shooting =
          nssleep(g_state.state.exposure_ns - delay_average_ns) == 0;
    }

    success = release_shutter(&end_ns);

    if (success)
      add_delay((end_ns - start_ns) - g_state.state.exposure_ns);
  }

  if (g_state.state.shooting &&
      ++g_state.state.frames_taken < g_state.state.frames) {
    async_queue_post(&g_main_queue, INTERVAL_DELAY, NULL, /*async*/ true);
  } else {
    MG_DEBUG(("Stop shooting"));
    g_state.state.shooting = false;
  }
}

static void start_shooting_command(void *data) {
  g_state.state.frames_taken = 0;
  g_state.state.shooting = true;

  update_shutter_speed();
  update_iso_speed();

  async_queue_post(&g_main_queue, INITIAL_DELAY, NULL, /*async*/ true);
}

static void stop_shooting_command(void *data) {
  g_state.state.shooting = false;
}

static void terminate_command(void *data) { g_state.state.running = false; }

static const char *command_names[] = {
    "NO_OP",          "INITIALIZE",    "DEINITIALIZE",   "CONNECT",
    "DISCONNECT",     "INITIAL_DELAY", "INTERVAL_DELAY", "TAKE_PICTURE",
    "START_SHOOTING", "STOP_SHOOTING", "TERMINATE",
};

typedef void (*command_handler_t)(void *);

static const command_handler_t command_table[] = {
    [NO_OP] = no_op_command,
    [INITIALIZE] = initialize_command,
    [DEINITIALIZE] = deinitialize_command,
    [CONNECT] = connect_command,
    [DISCONNECT] = disconnect_command,
    [INITIAL_DELAY] = initial_delay_command,
    [INTERVAL_DELAY] = interval_delay_command,
    [TAKE_PICTURE] = take_picture_command,
    [START_SHOOTING] = start_shooting_command,
    [STOP_SHOOTING] = stop_shooting_command,
    [TERMINATE] = terminate_command,
};

static void sig_handler(int sig) {
  disconnect_command(NULL);
  terminate_command(NULL);
  // async_queue_post(&g_main_queue, DISCONNECT, /*async*/ true);
  // async_queue_post(&g_main_queue, TERMINATE, /*async*/ true);
}

void command_processor(void) {
  signal(SIGTERM, sig_handler);
  signal(SIGINT, sig_handler);

  copy_all_exposures();
  copy_all_isos();

  while (g_state.state.running) {
    int32_t cmd = NO_OP;
    void *data = NULL;

    int32_t slot = async_queue_dequeue_locked(&g_main_queue, &cmd, &data,
                                              500 * MILLI_TO_NS);

    if (slot < 0) {
      EdsGetEvent();
      continue;
    }

    const char *command_name = command_names[cmd];
    command_handler_t handler = command_table[cmd];

    MG_DEBUG(("Command: %s on slot %d", command_name, slot));

    handler(data);

    async_queue_unlock(&g_main_queue, slot);
  }
}

void set_exposure_custom(const char *value_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int exposure = 0;
  if (sscanf(value_str, "%d", &exposure) == 1) {
    g_state.state.exposure_ns = exposure * SEC_TO_NS;
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_exposure_index(const char *index_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t index = 0;
  if (sscanf(index_str, "%d", &index) == 1) {
    g_state.state.exposure_index = index;

    update_shutter_speed();
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_iso_index(const char *index_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t index = 0;
  if (sscanf(index_str, "%d", &index) == 1) {
    g_state.state.iso_index = index;

    update_iso_speed();
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_delay(const char *value_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t delay = 0;
  if (sscanf(value_str, "%d", &delay) == 1) {
    g_state.state.delay_ns = delay * SEC_TO_NS;
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_interval(const char *value_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t interval = 0;
  if (sscanf(value_str, "%d", &interval) == 1) {
    g_state.state.interval_ns = interval * SEC_TO_NS;
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_frames(const char *value_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t frames = 0;
  if (sscanf(value_str, "%d", &frames) == 1) {
    g_state.state.frames = frames;
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void format_exposure(int64_t exposure, char *value_str, size_t size) {
  // if exposure >= 300ms use decimal format
  // otherwise use fractional format
  if (exposure >= 300 * MILLI_TO_NS) {
    float seconds = (float)exposure / SEC_TO_NS;
    snprintf(value_str, size, "%.1f\"", seconds);
  } else {
    int seconds = SEC_TO_NS / exposure;
    snprintf(value_str, size, "1/%d\"", seconds);
  }
}

void get_exposure_at(int32_t index, char *value_str, size_t size) {
  format_exposure(g_exposures[index].shutter_speed_ns, value_str, size);
}

int32_t get_exposure_count(void) { return g_exposures_size; }

void get_iso_at(int32_t index, char *value_str, size_t size) {
  strncpy(value_str, g_isos[index].iso_description, size);
}

int32_t get_iso_count(void) { return g_isos_size; }

static void fill_exposures(void) {
  g_exposures_size = 0;

  EdsPropertyDesc property_desc = {0};
  if (EdsGetPropertyDesc(g_state.camera, kEdsPropID_Tv, &property_desc) ==
      EDS_ERR_OK) {

    for (int i = 0; i < property_desc.numElements; i++) {
      EdsInt32 key = property_desc.propDesc[i];

      for (int j = 0; j < ALL_EXPOSURES_SIZE; j++) {
        if (g_all_exposures[j].shutter_param == key) {
          memcpy(&g_exposures[g_exposures_size++], &g_all_exposures[j],
                 sizeof(struct exposure_t));
          break;
        }
      }
    }
  }
}

static void fill_iso_speeds(void) {
  g_isos_size = 0;

  EdsPropertyDesc property_desc = {0};
  if (EdsGetPropertyDesc(g_state.camera, kEdsPropID_ISOSpeed, &property_desc) ==
      EDS_ERR_OK) {

    for (int i = 0; i < property_desc.numElements; i++) {
      EdsInt32 key = property_desc.propDesc[i];

      for (int j = 0; j < ALL_ISOS_SIZE; j++) {
        if (g_all_isos[j].iso_param == key) {
          memcpy(&g_isos[g_isos_size++], &g_all_isos[j], sizeof(struct iso_t));
          break;
        }
      }
    }
  }
}

static void copy_all_exposures(void) {
  g_exposures_size = ALL_EXPOSURES_SIZE;
  memcpy(g_exposures, g_all_exposures, sizeof(g_all_exposures));
}

static void copy_all_isos(void) {
  g_isos_size = ALL_ISOS_SIZE;
  memcpy(g_isos, g_all_isos, sizeof(g_all_isos));
}
