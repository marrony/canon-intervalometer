#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define MICRO_TO_NS 1000ull
#define MILLI_TO_NS 1000000ull
#define SEC_TO_NS 1000000000ull

int64_t get_delay_average(void);
void add_delay(int64_t delay);

#endif // TIMER_H
