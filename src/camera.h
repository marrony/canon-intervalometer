#ifndef CAMERA_H
#define CAMERA_H

// clang-format: off
#include <stdbool.h>
// clang-format: on

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

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
  int32_t iso_index;
  int32_t exposure_index;
  int32_t delay_us;
  int32_t exposure_us;
  int32_t interval_us;
  int32_t frames;
  int32_t frames_taken;
  bool initialized;
  bool connected;
  bool shooting;
  char description[EDS_MAX_NAME];
};

extern struct sync_queue_t g_main_queue;

void command_processor(void);

void get_copy_state(struct camera_state_t *state);
bool is_running(void);

void set_iso_index(const char *index_str);
void set_exposure_index(const char *index_str);
void set_exposure_custom(const char *value_str);
void set_delay(const char *value_str);
void set_interval(const char *value_str);
void set_frames(const char *value_str);

void get_exposure_at(int32_t index, char *value_str, size_t size);
int32_t get_exposure_count(void);

void get_iso_at(int32_t index, char *value_str, size_t size);
int32_t get_iso_count(void);

#endif // CAMERA_H
