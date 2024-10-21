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

#include "camera.h"
#include "mongoose.h"
#include "queue.h"
#include "timer.h"

static void fill_exposures(void);
static void fill_iso_speeds(void);
static void copy_all_exposures(void);
static void copy_all_isos(void);

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
            .iso_index = 0,
            .exposure_index = 0,
            .delay_us = 1 * SEC_TO_US,
            .exposure_us = 31 * SEC_TO_US,
            .interval_us = 1 * SEC_TO_US,
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
  const char *description;
  EdsUInt32 param;
};

struct iso_t {
  const char *description;
  EdsUInt32 param;
};

static const struct exposure_t g_all_exposures[] = {
    // {.shutter_speed_ns = 0xFFFFFFFFFFFFFF, .shutter_speed = 0x0C},
    {.description = "30\"", .param = 0x10},
    {.description = "25\"", .param = 0x13},
    {.description = "20\"", .param = 0x14},
    {.description = "15\"", .param = 0x18},
    {.description = "13\"", .param = 0x1B},
    {.description = "10\"", .param = 0x1C},
    {.description = "8\"", .param = 0x20},
    {.description = "6\"", .param = 0x24},
    {.description = "5\"", .param = 0x25},
    {.description = "4\"", .param = 0x28},
    {.description = "3\"2", .param = 0x2B},
    {.description = "3\"", .param = 0x2C},
    {.description = "2\"5", .param = 0x2D},
    {.description = "2\"", .param = 0x30},
    {.description = "1\"6", .param = 0x33},
    {.description = "1\"5", .param = 0x34},
    {.description = "1\"3", .param = 0x35},
    {.description = "1\"", .param = 0x38},
    {.description = "0\"8", .param = 0x3B},
    {.description = "0\"7", .param = 0x3C},
    {.description = "0\"6", .param = 0x3D},
    {.description = "0\"5", .param = 0x40},
    {.description = "0\"4", .param = 0x43},
    {.description = "0\"3", .param = 0x44},
    {.description = "1/4", .param = 0x48},
    {.description = "1/5", .param = 0x4B},
    {.description = "1/6", .param = 0x4C},
    {.description = "1/8", .param = 0x50},
    {.description = "1/10", .param = 0x54},
    {.description = "1/13", .param = 0x55},
    {.description = "1/15", .param = 0x58},
    {.description = "1/20", .param = 0x5C},
    {.description = "1/25", .param = 0x5D},
    {.description = "1/30", .param = 0x60},
    {.description = "1/40", .param = 0x63},
    {.description = "1/45", .param = 0x64},
    {.description = "1/50", .param = 0x65},
    {.description = "1/60", .param = 0x68},
    {.description = "1/80", .param = 0x6B},
    {.description = "1/90", .param = 0x6C},
    {.description = "1/100", .param = 0x6D},
    {.description = "1/125", .param = 0x70},
    {.description = "1/160", .param = 0x73},
    {.description = "1/180", .param = 0x74},
    {.description = "1/200", .param = 0x75},
    {.description = "1/250", .param = 0x78},
    {.description = "1/320", .param = 0x7B},
    {.description = "1/350", .param = 0x7C},
    {.description = "1/400", .param = 0x7D},
    {.description = "1/500", .param = 0x80},
    {.description = "1/640", .param = 0x83},
    {.description = "1/750", .param = 0x84},
    {.description = "1/800", .param = 0x85},
    {.description = "1/1000", .param = 0x88},
    {.description = "1/1250", .param = 0x8B},
    {.description = "1/1500", .param = 0x8C},
    {.description = "1/1600", .param = 0x8D},
    {.description = "1/2000", .param = 0x90},
    {.description = "1/2500", .param = 0x93},
    {.description = "1/3000", .param = 0x94},
    {.description = "1/3200", .param = 0x95},
    {.description = "1/4000", .param = 0x98},
    {.description = "1/5000", .param = 0x9B},
    {.description = "1/6000", .param = 0x9C},
    {.description = "1/6400", .param = 0x9D},
    {.description = "1/8000", .param = 0xA0},
    {.description = "1/10000", .param = 0xA3},
    {.description = "1/12800", .param = 0xA5},
    {.description = "1/16000", .param = 0xA8},
};

static const struct iso_t g_all_isos[] = {
    {.description = "Auto", .param = 0x0},
    {.description = "ISO 6", .param = 0x28},
    {.description = "ISO 12", .param = 0x30},
    {.description = "ISO 25", .param = 0x38},
    {.description = "ISO 50", .param = 0x40},
    {.description = "ISO 100", .param = 0x48},
    {.description = "ISO 125", .param = 0x4b},
    {.description = "ISO 160", .param = 0x4d},
    {.description = "ISO 200", .param = 0x50},
    {.description = "ISO 250", .param = 0x53},
    {.description = "ISO 320", .param = 0x55},
    {.description = "ISO 400", .param = 0x56},
    {.description = "ISO 500", .param = 0x5b},
    {.description = "ISO 640", .param = 0x5d},
    {.description = "ISO 800", .param = 0x60},
    {.description = "ISO 1000", .param = 0x63},
    {.description = "ISO 1250", .param = 0x65},
    {.description = "ISO 1600", .param = 0x68},
    {.description = "ISO 2000", .param = 0x6b},
    {.description = "ISO 2500", .param = 0x6d},
    {.description = "ISO 3200", .param = 0x70},
    {.description = "ISO 4000", .param = 0x73},
    {.description = "ISO 5000", .param = 0x75},
    {.description = "ISO 6400", .param = 0x78},
    {.description = "ISO 8000", .param = 0x07b},
    {.description = "ISO 10000", .param = 0x7d},
    {.description = "ISO 12800", .param = 0x80},
    {.description = "ISO 16000", .param = 0x83},
    {.description = "ISO 20000", .param = 0x85},
    {.description = "ISO 25600", .param = 0x88},
    {.description = "ISO 32000", .param = 0x8b},
    {.description = "ISO 40000", .param = 0x8d},
    {.description = "ISO 51200", .param = 0x90},
    {.description = "ISO 64000", .param = 0x3},
    {.description = "ISO 80000", .param = 0x95},
    {.description = "ISO 102400", .param = 0x98},
    {.description = "ISO 204800", .param = 0xa0},
    {.description = "ISO 409600", .param = 0xa8},
    {.description = "ISO 819200", .param = 0xb0},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define ALL_EXPOSURES_SIZE ARRAY_SIZE(g_all_exposures)
#define ALL_ISOS_SIZE ARRAY_SIZE(g_all_isos)

static struct exposure_t g_exposures[ALL_EXPOSURES_SIZE] = {0};
static int g_exposures_size = 0;

static struct iso_t g_isos[ALL_ISOS_SIZE] = {0};
static int g_isos_size = 0;

void get_state_copy(struct camera_state_t *state) {
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

#ifdef CAMERA_EVENTS
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
#else
static void attach_camera_callbacks(void) {}
#endif

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
  int64_t start = get_system_micros();
  EdsError err =
      EdsSendCommand(g_state.camera, kEdsCameraCommand_PressShutterButton,
                     kEdsCameraCommand_ShutterButton_Completely_NonAF);
  int64_t delta = get_system_micros() - start;

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Press Shutter err = %d", err));
    return false;
  }

  MG_DEBUG(("Press Button: %lld ms", delta / 1000));

  if (ts != NULL)
    *ts = start;

  return true;
}

static bool release_shutter(int64_t *ts) {
  int64_t start = get_system_micros();
  EdsError err =
      EdsSendCommand(g_state.camera, kEdsCameraCommand_PressShutterButton,
                     kEdsCameraCommand_ShutterButton_OFF);
  int64_t end = get_system_micros();

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Release Shutter err = %d", err));
    return false;
  }

  int64_t delta = end - start;
  MG_DEBUG(("Release Button: %lld ms", delta / 1000));

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
  EdsError err = EdsSetPropertyData(g_state.camera, kEdsPropID_Tv, 0,
                                    sizeof(EdsUInt32), &shutter_speed);

  if (err != EDS_ERR_OK) {
    MG_DEBUG(("Error setting shutter speed"));
  }
}

static void set_iso_speed(EdsUInt32 iso_speed) {
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
    struct exposure_t exposure = g_exposures[g_state.state.exposure_index];
    MG_DEBUG(("Setting shutter speed = %s", exposure.description));
    set_shutter_speed(exposure.param);
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
    struct iso_t iso = g_isos[g_state.state.iso_index];
    MG_DEBUG(("Setting to ISO = %s", iso.description));
    set_iso_speed(iso.param);
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
    MG_DEBUG(("Session opened"));
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
  if (g_state.state.delay_us > 0) {
    if (!ussleep(g_state.state.delay_us)) {
      g_state.state.shooting = false;
      return;
    }
  }

  async_queue_post(&g_main_queue, TAKE_PICTURE, NULL, /*async*/ true);
}

static void interval_delay_command(void *data) {
  if (ussleep(g_state.state.interval_us)) {
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
    int64_t delay_average_us = get_delay_average();

    int64_t start_us, end_us;

    bool success = press_shutter(&start_us);

    if (success) {
      g_state.state.shooting =
          ussleep(g_state.state.exposure_us - delay_average_us);
    }

    success = release_shutter(&end_us);

    if (success)
      add_delay((end_us - start_us) - g_state.state.exposure_us);
  }

  if (g_state.state.shooting &&
      ++g_state.state.frames_taken < g_state.state.frames) {
    async_queue_post(&g_main_queue, INTERVAL_DELAY, NULL, /*async*/ true);
  } else {
    MG_DEBUG(("Stop shooting"));
    g_state.state.shooting = false;
  }
}

static void take_single_picture_command(void *data) {
  intptr_t frames = g_state.state.frames;
  g_state.state.frames = 1;
  async_queue_post(&g_main_queue, TAKE_PICTURE, NULL, /*async*/ true);
  async_queue_post(&g_main_queue, SET_FRAMES, (void *)frames, /*async*/ true);
}

static void set_frames_command(void *data) {
  int32_t frames = (intptr_t)data;
  g_state.state.frames = frames;
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

static void terminate_command(void *data) {
  MG_DEBUG(("Terminating"));
  g_state.state.running = false;
}

static const char *command_names[] = {
    "NO_OP",          "INITIALIZE",     "DEINITIALIZE",
    "CONNECT",        "DISCONNECT",     "INITIAL_DELAY",
    "INTERVAL_DELAY", "TAKE_PICTURE",   "TAKE_SINGLE_PICTURE",
    "SET_FRAMES",     "START_SHOOTING", "STOP_SHOOTING",
    "TERMINATE",
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
    [TAKE_SINGLE_PICTURE] = take_single_picture_command,
    [START_SHOOTING] = start_shooting_command,
    [STOP_SHOOTING] = stop_shooting_command,
    [TERMINATE] = terminate_command,
};

static void sig_handler(int sig) {
  disconnect_command(NULL);
  terminate_command(NULL);
  // async_queue_post(&g_main_queue, DISCONNECT, /*async*/ true);
  // async_queue_post(&g_main_queue, TERMINATE, /*async*/ true);
  exit(0);
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
    g_state.state.exposure_us = exposure * SEC_TO_US;
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
    g_state.state.delay_us = delay * SEC_TO_US;
  }

  assert(pthread_mutex_unlock(&g_state.mutex) == 0);
}

void set_interval(const char *value_str) {
  assert(pthread_mutex_lock(&g_state.mutex) == 0);

  int32_t interval = 0;
  if (sscanf(value_str, "%d", &interval) == 1) {
    g_state.state.interval_us = interval * SEC_TO_US;
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

void get_exposure_at(int32_t index, char *value_str, size_t size) {
  strncpy(value_str, g_exposures[index].description, size);
}

int32_t get_exposure_count(void) { return g_exposures_size; }

void get_iso_at(int32_t index, char *value_str, size_t size) {
  strncpy(value_str, g_isos[index].description, size);
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
        if (g_all_exposures[j].param == key) {
          memcpy(&g_exposures[g_exposures_size++], &g_all_exposures[j],
                 sizeof(struct exposure_t));
          break;
        }
      }
    }
  } else {
    MG_DEBUG(("Error getting shutter speeds"));
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
        if (g_all_isos[j].param == key) {
          memcpy(&g_isos[g_isos_size++], &g_all_isos[j], sizeof(struct iso_t));
          break;
        }
      }
    }
  } else {
    MG_DEBUG(("Error getting iso speeds"));
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
