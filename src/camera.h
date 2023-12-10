#ifndef CAMERA_H
#define CAMERA_H

// clang-format: off
#include <stdbool.h>
// clang-format: on

#include <EDSDK.h>
#include <pthread.h>

#include "queue.h"

struct camera_state_t {
  pthread_mutex_t mutex;
  bool running;
  long delay;
  long exposure;
  long interval;
  long frames;
  long frames_taken;
  bool initialized;
  bool connected;
  bool shooting;
  char description[EDS_MAX_NAME];
  EdsCameraRef camera;
};

extern struct camera_state_t g_state;
extern struct sync_queue_t g_queue;

bool is_shooting();

#endif // CAMERA_H
