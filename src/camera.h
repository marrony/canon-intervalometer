#ifndef CAMERA_H
#define CAMERA_H

// clang-format: off
#include <stdbool.h>
// clang-format: on

#include "queue.h"

#include <EDSDK.h>
#include <pthread.h>

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

struct camera_state_t {
  pthread_mutex_t mutex;
  bool running;
  int64_t delay_ns;
  int64_t exposure_ns;
  int64_t interval_ns;
  int64_t frames;
  int64_t frames_taken;
  bool initialized;
  bool connected;
  bool shooting;
  char description[EDS_MAX_NAME];
  EdsCameraRef camera;
};

extern struct sync_queue_t g_main_queue;

void command_processor();

void get_copy_state(struct camera_state_t *state);
bool is_running();

void set_exposure(const char *value_str);
void set_delay(const char *value_str);
void set_interval(const char *value_str);
void set_frames(const char *value_str);

void get_exposure(char *value_str, size_t size);

#endif // CAMERA_H
