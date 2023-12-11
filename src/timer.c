#include "timer.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#define DELAYS_SIZE 32

static struct {
  bool aborted;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int64_t delays[DELAYS_SIZE];
  int delays_start;
  int delays_length;
} g_timer = {
    .aborted = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .delays = {0},
    .delays_start = 0,
    .delays_length = 0,
};

void adjust_timer_ns(struct timespec *ts, int64_t timer_ns) {
  clock_gettime(CLOCK_REALTIME, ts);

  ts->tv_sec += timer_ns / SEC_TO_NS;
  ts->tv_nsec += timer_ns % SEC_TO_NS;

  ts->tv_sec += ts->tv_nsec / SEC_TO_NS;
  ts->tv_nsec = ts->tv_nsec % SEC_TO_NS;
}

bool start_timer_ns(int64_t timer_ns) {
  struct timespec ts = {0, 0};
  adjust_timer_ns(&ts, timer_ns);

  assert(pthread_mutex_lock(&g_timer.mutex) == 0);

  {
    g_timer.aborted = false;
    int ret = pthread_cond_timedwait(&g_timer.cond, &g_timer.mutex, &ts);
    assert(ret != EINVAL);
    // 0 means cond was triggered, otherwise ETIMEDOUT is returned
    g_timer.aborted = ret == 0;
  }

  bool aborted = g_timer.aborted;
  assert(pthread_mutex_unlock(&g_timer.mutex) == 0);
  return !aborted;
}

void abort_timer() {
  // at this point timer_mutex is unlock wating for the condition
  assert(pthread_mutex_lock(&g_timer.mutex) == 0);
  if (!g_timer.aborted)
    pthread_cond_broadcast(&g_timer.cond);
  g_timer.aborted = true;
  assert(pthread_mutex_unlock(&g_timer.mutex) == 0);
}

void add_delay(int64_t delay) {
  g_timer.delays[(g_timer.delays_start + g_timer.delays_length) % DELAYS_SIZE] =
      delay;

  if (g_timer.delays_length < DELAYS_SIZE) {
    g_timer.delays_length++;
  } else {
    g_timer.delays_start = (g_timer.delays_start + 1) % DELAYS_SIZE;
  }
}

int64_t get_delay_average() {
  if (g_timer.delays_length == 0)
    return 0;

  int64_t sum = 0;

  for (int i = 0; i < g_timer.delays_length; i++)
    sum += g_timer.delays[(g_timer.delays_start + i) % DELAYS_SIZE];

  return sum / g_timer.delays_length;
}
