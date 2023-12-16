#include "timer.h"

#define DELAYS_SIZE 32

static struct {
  int64_t delays[DELAYS_SIZE];
  int delays_start;
  int delays_length;
} g_timer = {
    .delays = {0},
    .delays_start = 0,
    .delays_length = 0,
};

void add_delay(int64_t delay) {
  g_timer.delays[(g_timer.delays_start + g_timer.delays_length) % DELAYS_SIZE] =
      delay;

  if (g_timer.delays_length < DELAYS_SIZE) {
    g_timer.delays_length++;
  } else {
    g_timer.delays_start = (g_timer.delays_start + 1) % DELAYS_SIZE;
  }
}

int64_t get_delay_average(void) {
  if (g_timer.delays_length == 0)
    return 0;

  int64_t sum = 0;

  for (int i = 0; i < g_timer.delays_length; i++)
    sum += g_timer.delays[(g_timer.delays_start + i) % DELAYS_SIZE];

  return sum / g_timer.delays_length;
}
