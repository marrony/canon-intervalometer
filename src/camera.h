#ifndef CAMERA_H
#define CAMERA_H

// clang-format: off
#include <stdbool.h>
// clang-format: on

#include "queue.h"

#include <EDSDK.h>

enum command_type {
  NO_OP,
  INITIALIZE,
  DEINITIALIZE,
  CONNECT,
  DISCONNECT,
  INITIAL_DELAY,
  INTERVAL_DELAY,
  TAKE_PICTURE,
  START_SHOOTING,
  STOP_SHOOTING,
  TERMINATE,
};

struct camera_state_t {
  bool running;
  int64_t delay_ns;
  int32_t exposure_index;
  int64_t exposure_ns;
  int64_t interval_ns;
  int32_t frames;
  int32_t frames_taken;
  bool initialized;
  bool connected;
  bool shooting;
  char description[EDS_MAX_NAME];
};

extern struct sync_queue_t g_main_queue;

void command_processor();

void get_copy_state(struct camera_state_t *state);
bool is_running();

void set_exposure_index(const char *index_str);
void set_exposure_custom(const char *value_str);
void set_delay(const char *value_str);
void set_interval(const char *value_str);
void set_frames(const char *value_str);

void get_exposure_at(int32_t index, char *value_str, size_t size);
void format_exposure(int64_t value, char *value_str, size_t size);
int32_t get_exposure_count();

#endif // CAMERA_H
