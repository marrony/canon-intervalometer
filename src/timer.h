#ifndef TIMER_H
#define TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MICRO_TO_NS 1000ull
#define MILLI_TO_NS 1000000ull
#define SEC_TO_NS 1000000000ull

void adjust_timer_ns(struct timespec *ts, int64_t timer_ns);
bool start_timer_ns(int64_t timer_ns);
void abort_timer(void);

int64_t get_delay_average(void);
void add_delay(int64_t delay);

#endif // TIMER_H
