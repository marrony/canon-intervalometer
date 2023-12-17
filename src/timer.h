#ifndef TIMER_H
#define TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define MICRO_TO_NS 1000ull
#define MILLI_TO_NS 1000000ull
#define SEC_TO_NS 1000000000ull
#define SEC_TO_US 1000000ull

int32_t get_delay_average(void);
void add_delay(int32_t delay);

bool ussleep(int32_t timer_us);
bool nssleep(int64_t timer_ns);
int64_t get_system_micros(void);

#endif // TIMER_H
