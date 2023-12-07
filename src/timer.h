#ifndef TIMER_H
#define TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define NANOS (MICROS * 1000ll)
#define MICROS (MILLIS * 1000ll)
#define MILLIS (1000ll)

void adjust_timer_ns(struct timespec *ts, int64_t timer_ns);
bool start_timer_ns(int64_t timer_ns);
void abort_timer();

int64_t get_delay_average();
void add_delay(int64_t delay);

#endif // TIMER_H
