#include "timer.h"

#include <errno.h>
#include <time.h>

#define DELAYS_SIZE 32

static struct {
  int32_t delays[DELAYS_SIZE];
  int delays_start;
  int delays_length;
} g_timer = {
    .delays = {0},
    .delays_start = 0,
    .delays_length = 0,
};

void add_delay(int32_t delay) {
  g_timer.delays[(g_timer.delays_start + g_timer.delays_length) % DELAYS_SIZE] =
      delay;

  if (g_timer.delays_length < DELAYS_SIZE) {
    g_timer.delays_length++;
  } else {
    g_timer.delays_start = (g_timer.delays_start + 1) % DELAYS_SIZE;
  }
}

int32_t get_delay_average(void) {
  if (g_timer.delays_length == 0)
    return 0;

  int64_t sum = 0;

  for (int i = 0; i < g_timer.delays_length; i++)
    sum += g_timer.delays[(g_timer.delays_start + i) % DELAYS_SIZE];

  return sum / g_timer.delays_length;
}

bool ussleep(int32_t timer_us) { return nssleep(timer_us * MICRO_TO_NS); }

bool nssleep(int64_t timer_ns) {
  struct timespec ts = {
      .tv_sec = timer_ns / SEC_TO_NS,
      .tv_nsec = timer_ns % SEC_TO_NS,
  };
  struct timespec rem = {0, 0};

  while (nanosleep(&ts, &rem) < 0) {
    if (errno != EINTR)
      return false;
    ts = rem;
  }

  return true;
}

int64_t get_system_micros(void) {
  struct timespec ts = {0, 0};
  timespec_get(&ts, TIME_UTC);
  return (int64_t)(ts.tv_sec * SEC_TO_US) + (int64_t)(ts.tv_nsec / MICRO_TO_NS);
}
