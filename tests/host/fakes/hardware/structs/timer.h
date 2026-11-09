// Host-test stand-in: timer_hw->timerawl reads the current time in us
#pragma once
#include <stdint.h>
typedef struct { uint32_t timerawl; } fake_timer_t;
fake_timer_t *fake_timer(void);
#define timer_hw (fake_timer())
