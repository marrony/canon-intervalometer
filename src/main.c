#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <pthread.h>
#include <EDSDK.h>
#include <EDSDKTypes.h>

#include "mongoose.h"

extern __uint64_t __thread_selfid( void );

static int64_t get_system_nanos() {
  struct timespec ts = {0, 0};
#if defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#elif defined(CLOCK_MONOTONIC)
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  return (int64_t) ts.tv_sec * 1000000000 + (int64_t) ts.tv_nsec;
}

static int nssleep(int64_t timer_ns) {
  struct timespec ts = {0, timer_ns};
  struct timespec rem = {0, 0};

  while (nanosleep(&ts, &rem) < 0) {
    if (errno != EINTR)
      return -1;
    ts = rem;
  }

  return 0;
}

static void shooting_nssleep(int64_t timer_ns) {
  if (timer_ns > 0 && nssleep(timer_ns) < 0)
    MG_INFO(("failed to nssleep"));
}

static const char *s_http_addr = "http://0.0.0.0:8001";  // HTTP port
static char s_root_dir[PATH_MAX+1] = {0};

static void fix_root_dir(const char* argv) {
  char temp_dir[PATH_MAX+1] = {0};

  realpath(argv, temp_dir);
  snprintf(s_root_dir, PATH_MAX, "%s/web_root", dirname(temp_dir));
}

static bool g_initialized = false;
static bool g_connected = false;
static bool g_shooting = false;
static long g_camera = 0;
static long g_delay = 5;
static long g_exposure = 30;
static long g_interval = 5;
static long g_frames = 50;
static long g_frames_taken = 0;

static bool initialize() {
  if (!g_initialized) {
    if (EdsInitializeSDK() == EDS_ERR_OK) {
      g_initialized = true;
      sleep(1);
    }
  }

  return g_initialized;
}

static void deinitialize() {
  g_initialized = false;
  g_connected = false;
  g_camera = 0;
  EdsTerminateSDK();
}

#define MAX_CAMERAS 4

static EdsUInt32 g_camera_count = 0;
struct camera_t {
  char description[EDS_MAX_NAME];
  EdsCameraRef camera;
};
static struct camera_t g_cameras[MAX_CAMERAS] = { 0 };

static size_t print_cameras(void (*out)(char, void *), void *ptr, va_list *ap) {
  size_t len = 0;

  for (EdsUInt32 i = 0; i < g_camera_count; i++) {
    len += mg_xprintf(out, ptr,
      "{%m:%d,%m:%m,%m:%d}",
      MG_ESC("id"), i + 1,
      MG_ESC("description"), MG_ESC(g_cameras[i].description),
      MG_ESC("handle"), MG_ESC(g_cameras[i].camera)
    );

    if (i < g_camera_count - 1) {
      len += mg_xprintf(out, ptr, ",");
    }
  }

  return len;
}

static size_t print_state(void (*out)(char, void *), void *ptr, va_list *ap) {
  return mg_xprintf(out, ptr,
    "{%m:%s,%m:%s,%m:%ld,%m:%ld,%m:%ld,%m:%ld,%m:%ld,%m:%ld}",
    MG_ESC("shooting"), g_shooting ? "true" : "false",
    MG_ESC("connected"), g_connected ? "true" : "false",
    MG_ESC("camera"), g_camera,
    MG_ESC("delay"), g_delay,
    MG_ESC("exposure"), g_exposure,
    MG_ESC("interval"), g_interval,
    MG_ESC("frames"), g_frames,
    MG_ESC("frames_taken"), g_frames_taken
  );
}

static void serialize_state(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:%M}\n",
    MG_ESC("status"), MG_ESC("success"),
    MG_ESC("state"), print_state
  );
}

static void serialize_camera_list(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:[%M],%m:%M}\n",
    MG_ESC("status"), MG_ESC("success"),
    MG_ESC("cameras"), print_cameras,
    MG_ESC("state"), print_state
  );
}

static void internal_server_error(struct mg_connection* c) {
  mg_http_reply(
    c, 500,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{\"status\": \"failure\"}\n"
  );
}

static void serialize_success(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m}\n",
    MG_ESC("status"), MG_ESC("success")
  );
}

static void serialize_failure(struct mg_connection* c, const char* description) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:%m}\n",
    MG_ESC("status"), MG_ESC("failure"),
    MG_ESC("description"), MG_ESC(description)
  );
}

static void not_found(struct mg_connection* c) {
  mg_http_reply(
    c, 404,
    "Access-Control-Allow-Origin: *\r\n",
    "Not Found"
  );
}

static bool build_camera_list() {
  if (!g_initialized) {
    return false;
  }

  EdsCameraListRef camera_list = NULL;

  if (EdsGetCameraList(&camera_list) == EDS_ERR_OK) {
    EdsUInt32 count = 0;

    if (EdsGetChildCount(camera_list, &count) == EDS_ERR_OK) {
      if (count == 0 || count > MAX_CAMERAS) {
        EdsRelease(camera_list);
        return false;
      }

      for (EdsUInt32 i = 0; i < count; i++) {
        EdsCameraRef camera_ref = NULL;
        EdsDeviceInfo device_info;

        if (EdsGetChildAtIndex(camera_list, i, &camera_ref) == EDS_ERR_OK) {
          if (camera_ref != NULL && EdsGetDeviceInfo(camera_ref, &device_info) == EDS_ERR_OK) {
            g_cameras[i].camera = camera_ref;
            strncpy(g_cameras[i].description, device_info.szDeviceDescription, EDS_MAX_NAME);
          } else {
            EdsRelease(camera_ref);
            EdsRelease(camera_list);
            return false;
          }
        } else {
          EdsRelease(camera_list);
          return false;
        }
      }

      g_camera_count = count;
    } else {
      EdsRelease(camera_list);
      return false;
    }
  } else {
    return false;
  }

  EdsGetEvent();
  EdsRelease(camera_list);
  return true;
}

EdsError EDSCALLBACK handleObjectEvent(EdsObjectEvent event, EdsBaseRef object, EdsVoid *context) {
  EdsError err = EDS_ERR_OK;

  // Object must be released if(object)
  EdsRelease(object);
  //_syncObject->unlock();
  return err;
}

EdsError EDSCALLBACK handlePropertyEvent(
  EdsUInt32 inEvent,
  EdsUInt32 inPropertyID,
  EdsUInt32 inParam,
  EdsVoid *inContext) {
  EdsError err = EDS_ERR_OK;
  // do something
  return err;
}

EdsError EDSCALLBACK handleSateEvent(EdsStateEvent event, EdsUInt32 parameter, EdsVoid *context) {
  EdsError err = EDS_ERR_OK;
  // do something
  return err;
}

#define CHECK_INIT_AND_CONNECTED(c)                    \
  do {                                                 \
    if (!g_initialized) {                              \
      return serialize_failure((c), "Not initialized");\
    }                                                  \
    if (!g_connected) {                                \
      return serialize_failure((c), "Not connected");  \
    }                                                  \
  } while (false)

static int64_t shutter_pressed;
static void press_shutter() {
  EdsCameraRef camera = g_cameras[g_camera - 1].camera;

  EdsSendCommand(
    camera,
    kEdsCameraCommand_PressShutterButton,
    kEdsCameraCommand_ShutterButton_Completely_NonAF
  );
  shutter_pressed = get_system_nanos();
}

static void release_shutter() {
  EdsCameraRef camera = g_cameras[g_camera - 1].camera;

  EdsSendCommand(
    camera,
    kEdsCameraCommand_PressShutterButton,
    kEdsCameraCommand_ShutterButton_OFF
  );
  int64_t delta = get_system_nanos() - shutter_pressed;
  MG_INFO(("Exposure %lld", delta));
}

static void take_picture() {
  press_shutter();
  release_shutter();
}

enum shooting_state {
  DELAY_STATE,
  PRESS_SHUTTER_STATE,
  RELEASE_SHUTTER_STATE,
  INTERVAL_STATE,
  END_STATE
};

struct shooting_data_t {
  enum shooting_state state;
  int64_t timer_ns;
};

static struct shooting_data_t shooting_data = {
  .state = END_STATE,
  .timer_ns = 0
};

#define TIMER_GRANULARITY_MS 100

//todo: calculate the real elapsed time
static void shooting_timer(void* data) {
  static int64_t last_time = 0;

  int64_t now = get_system_nanos();
  int64_t elapsed_ns = now - last_time;
  last_time = now;

  switch (shooting_data.state) {
    case DELAY_STATE:
      shooting_data.timer_ns -= elapsed_ns;
      if (shooting_data.timer_ns <= TIMER_GRANULARITY_MS*1000*1000) {
        shooting_nssleep(shooting_data.timer_ns);
        shooting_data.state = PRESS_SHUTTER_STATE;
      }

      break;

    case PRESS_SHUTTER_STATE:
      press_shutter();
      shooting_data.state = RELEASE_SHUTTER_STATE;
      shooting_data.timer_ns = g_exposure * 1000 * 1000 * 1000;
      break;

    case RELEASE_SHUTTER_STATE:
      shooting_data.timer_ns -= elapsed_ns;
      if (shooting_data.timer_ns <= TIMER_GRANULARITY_MS*1000*1000) {
        shooting_nssleep(shooting_data.timer_ns);
        release_shutter();

        if (++g_frames_taken < g_frames) {
          shooting_data.state = INTERVAL_STATE;
          shooting_data.timer_ns = g_interval * 1000 * 1000 * 1000;
        } else {
          shooting_data.state = END_STATE;
        }
      }

      break;

    case INTERVAL_STATE:
      shooting_data.timer_ns -= elapsed_ns;
      if (shooting_data.timer_ns <= TIMER_GRANULARITY_MS*1000*1000) {
        shooting_nssleep(shooting_data.timer_ns);
        shooting_data.state = PRESS_SHUTTER_STATE;
      }

      break;

    case END_STATE:
      g_shooting = false;
      break;
  }

  last_time = get_system_nanos();
}

static void start_shooting(struct mg_mgr *mgr) {
  g_shooting = true;
  g_frames_taken = 0;

  shooting_data.state = DELAY_STATE;
  shooting_data.timer_ns = g_delay * 1000 * 1000 * 1000;
}

static void evt_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  (void) fn_data;

  if (ev == MG_EV_POLL) {
  }

  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    struct mg_str method = hm->method;

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (is_get && mg_http_match_uri(hm, "/api/cameras")) {
      if (!initialize()) {
        MG_INFO(("Could not initialize"));
        return internal_server_error(c);
      }

      if (g_camera_count == 0 && !build_camera_list()) {
        MG_INFO(("Could not build camera list"));
        deinitialize();
        return internal_server_error(c);
      }

      serialize_camera_list(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/connect")) {
      if (!g_initialized) {
        return serialize_failure(c, "Not initialized");
      }

      if (g_connected) {
        return serialize_failure(c, "Already connected");
      }

      struct mg_str json = hm->body;

      long camera_id = mg_json_get_long(json, "$.camera", 0);

      if (camera_id <= 0 || camera_id > g_camera_count) {
        return serialize_failure(c, "Invalid camera id");
      }

      if (EdsOpenSession(g_cameras[camera_id - 1].camera) == EDS_ERR_OK) {
        g_connected = true;
        g_camera = camera_id;

        serialize_state(c);
      } else {
        serialize_failure(c, "Error connecting to the camera");
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/disconnect")) {
      if (!g_initialized) {
        return serialize_failure(c, "Not initialized");
      }

      struct mg_str json = hm->body;

      long camera_id = mg_json_get_long(json, "$.camera", 0);

      if (camera_id <= 0 || camera_id > g_camera_count) {
        return serialize_failure(c, "Invalid camera id");
      }

      g_connected = false;

      if (EdsCloseSession(g_cameras[camera_id - 1].camera) == EDS_ERR_OK) {
        //
      }

      serialize_success(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/start-shoot")) {
      CHECK_INIT_AND_CONNECTED(c);

      struct mg_str json = hm->body;

      long camera_id = mg_json_get_long(json, "$.camera", 0);
      g_delay = mg_json_get_long(json, "$.delay", -1);
      g_exposure = mg_json_get_long(json, "$.exposure", -1);
      g_interval = mg_json_get_long(json, "$.interval", -1);
      g_frames = mg_json_get_long(json, "$.frames", -1);

      if (
        camera_id <= 0 || g_delay < 0 || g_exposure < 0 ||
        g_interval < 0 || g_frames < 0) {
        return serialize_failure(c, "Invalid arguments");
      }

      start_shooting(c->mgr);

      serialize_state(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/stop-shoot")) {
      CHECK_INIT_AND_CONNECTED(c);

      g_shooting = false;

      serialize_state(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/take-picture")) {
      CHECK_INIT_AND_CONNECTED(c);

      struct mg_str json = hm->body;

      long camera_id = mg_json_get_long(json, "$.camera", 0);

      if (camera_id <= 0 || camera_id > g_camera_count) {
        return serialize_failure(c, "Invalid camera id");
      }

      take_picture();

      serialize_state(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/state")) {
      CHECK_INIT_AND_CONNECTED(c);

      serialize_state(c);
    } else if (is_get) {
      struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
      mg_http_serve_dir(c, ev_data, &opts);
    } else {
      not_found(c);
    }
  }
}

int main(int argc, char* argv[]) {
  fix_root_dir(argv[0]);

  struct mg_mgr mgr;
  mg_log_set(MG_LL_DEBUG);
  mg_mgr_init(&mgr);
  mg_timer_add(&mgr, TIMER_GRANULARITY_MS, MG_TIMER_REPEAT, shooting_timer, &mgr);
  mg_http_listen(&mgr, s_http_addr, evt_handler, NULL);

  for (;;) {
    mg_mgr_poll(&mgr, TIMER_GRANULARITY_MS);

    if (g_initialized)
      EdsGetEvent();
  }

  mg_mgr_free(&mgr);
  return 0;
}
