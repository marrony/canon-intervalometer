// Copyright (c) 2022 Cesanta Software Limited
// All rights reserved
//
// REST basics example
// It implements the following endpoints:
//    /api/f1 - respond with a simple mock result
//    /api/sum - respond with the result of adding two numbers
//    any other URI serves static files from s_root_dir
// Results are JSON strings

#include <stdlib.h>
#include <libgen.h>
#include <pthread.h>
#include <EDSDK.h>
#include <EDSDKTypes.h>

#include "mongoose.h"

extern __uint64_t __thread_selfid( void );

static const char *s_http_addr = "http://0.0.0.0:8000";  // HTTP port
static char s_root_dir[PATH_MAX+1] = {0};

static void fix_root_dir(const char* argv) {
  char temp_dir[PATH_MAX+1] = {0};

  realpath(argv, temp_dir);
  snprintf(s_root_dir, PATH_MAX, "%s/web_root", dirname(temp_dir));
}

static bool g_initialized = false;
static bool g_connected = false;
static bool g_shooting = false;
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
  EdsTerminateSDK();
}

#define MAX_CAMERAS 4

static EdsUInt32 g_camera_count = 0;
struct camera_t {
  char description[EDS_MAX_NAME];
  EdsCameraRef camera;
};
static struct camera_t g_cameras[MAX_CAMERAS] = { 0 };

static void serialize_state(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:{%m:%s,%m:%d,%m:%d,%m:%d,%m:%d,%m:%d}}\n",
    MG_ESC("status"), MG_ESC("success"),
    MG_ESC("state"),
    MG_ESC("shooting"), g_shooting ? "true" : "false",
    MG_ESC("delay"), g_delay,
    MG_ESC("exposure"), g_exposure,
    MG_ESC("interval"), g_interval,
    MG_ESC("frames"), g_frames,
    MG_ESC("frames_taken"), g_frames_taken
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

static size_t print_cameras(void (*out)(char, void *), void *ptr, va_list *ap) {
  size_t len = 0;
  EdsUInt32 num = va_arg(*ap, EdsUInt32);
  const struct camera_t *arr = va_arg(*ap, const struct camera_t*);

  for (EdsUInt32 i = 0; i < num; i++) {
    len += mg_xprintf(out, ptr,
      "{%m:%d,%m:%m,%m:%d}",
      MG_ESC("id"), i + 1, 
      MG_ESC("description"), MG_ESC(arr[i].description),
      MG_ESC("handle"), MG_ESC(arr[i].camera)
    );

    if (i < num - 1) {
      len += mg_xprintf(out, ptr, ",");
    }
  }

  return len;
}

static void serialize_camera_list(struct mg_connection* c) {
  mg_http_reply(
    c, 200,
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n",
    "{%m:%m,%m:[%M]}\n",
    MG_ESC("status"), MG_ESC("success"),
    MG_ESC("cameras"), print_cameras, g_camera_count, g_cameras
  );
}

EdsError EDSCALLBACK handleObjectEvent(EdsObjectEvent event, EdsBaseRef object, EdsVoid *context)
{
	EdsError err = EDS_ERR_OK;

	// Object must be released if(object)
	{
		EdsRelease(object);
	}
	//_syncObject->unlock();
	return err;
}

EdsError EDSCALLBACK handlePropertyEvent(
	EdsUInt32 inEvent,
	EdsUInt32 inPropertyID,
	EdsUInt32 inParam,
	EdsVoid *inContext)
{
	EdsError err = EDS_ERR_OK;
	// do something
	return err;
}

EdsError EDSCALLBACK handleSateEvent(EdsStateEvent event, EdsUInt32 parameter, EdsVoid *context)
{
	EdsError err = EDS_ERR_OK;
	// do something
	return err;
}

static void evt_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  (void) fn_data;

  if (ev == MG_EV_POLL) {
  }

  if (ev == MG_EV_HTTP_MSG) {
    if (!initialize()) {
      MG_INFO(("Could not initialize"));
      return internal_server_error(c);
    }

    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    struct mg_str method = hm->method;

    bool is_get = mg_vcmp(&method, "GET") == 0;
    bool is_post = mg_vcmp(&method, "POST") == 0;

    if (is_get && mg_http_match_uri(hm, "/api/cameras")) {
      if (g_camera_count == 0 && !build_camera_list()) {
        MG_INFO(("Could not build camera list"));
        deinitialize();
        return internal_server_error(c);
      }

      serialize_camera_list(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/connect")) {
      if (g_connected) {
        return serialize_failure(c, "Already connected");
      } else {
        struct mg_str json = hm->body;

        long camera_id = mg_json_get_long(json, "$.camera", 0);

        if (camera_id <= 0 || camera_id > g_camera_count) {
          return serialize_failure(c, "Invalid camera id");
        }

        if (EdsOpenSession(g_cameras[camera_id - 1].camera) == EDS_ERR_OK) {
          g_connected = true;

          serialize_state(c);
        } else {
          serialize_failure(c, "Error connecting to the camera");
        }
      }
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/disconnect")) {
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
      struct mg_str json = hm->body;

      //long camera_id = mg_json_get_long(json, "$.camera", -1);
      g_delay = mg_json_get_long(json, "$.delay", -1);
      g_exposure = mg_json_get_long(json, "$.exposure", -1);
      g_interval = mg_json_get_long(json, "$.interval", -1);
      g_frames = mg_json_get_long(json, "$.frames", -1);

      g_shooting = true;
      g_frames_taken = 0;

      serialize_state(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/stop-shoot")) {
      g_shooting = false;

      serialize_state(c);
    } else if (is_post && mg_http_match_uri(hm, "/api/camera/state")) {
      if (g_frames_taken++ > 10) {
        g_shooting = false;
        g_frames_taken = 0;
      }

      serialize_state(c);
    } else if (is_get) {
      struct mg_http_serve_opts opts = {.root_dir = s_root_dir};
      mg_http_serve_dir(c, ev_data, &opts);
    } else {
      serialize_failure(c, "not found");
    }
  }
}

int main(int argc, char* argv[]) {
  fix_root_dir(argv[0]);

  struct mg_mgr mgr;
  mg_log_set(MG_LL_DEBUG);
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, s_http_addr, evt_handler, NULL);

  for (;;) {
    mg_mgr_poll(&mgr, 1000);

    if (g_initialized)
      EdsGetEvent();
  }

  mg_mgr_free(&mgr);
  return 0;
}
