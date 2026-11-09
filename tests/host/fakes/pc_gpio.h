// Host-test stand-in for pc_gpio.h: SIO hi-bank writes are logged with the
// PIO E-cycle count at the time, and applied to fake_gpio_hi_out.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FAKE_GPIO_SET, FAKE_GPIO_CLR, FAKE_GPIO_TOGL };

typedef struct fake_gpio_write_t {
	uint8_t op;     // FAKE_GPIO_*
	uint32_t mask;  // bit n = GPIO 32+n
	uint32_t cycle; // eclock_get_pio_cycles() at the write
} fake_gpio_write_t;

#define FAKE_GPIO_LOG_SIZE 4096
extern fake_gpio_write_t fake_gpio_log[FAKE_GPIO_LOG_SIZE];
extern volatile uint32_t fake_gpio_log_count;
extern volatile uint32_t fake_gpio_hi_out;
extern uint64_t fake_gpio_outputs; // pins configured as outputs (bit = GPIO number)

void fake_gpio_hi_write(uint8_t op, uint32_t mask);
void fake_gpio_reset_log(void);

static inline void pc_gpio_hi_set(uint32_t mask) { fake_gpio_hi_write(FAKE_GPIO_SET, mask); }
static inline void pc_gpio_hi_clr(uint32_t mask) { fake_gpio_hi_write(FAKE_GPIO_CLR, mask); }
static inline void pc_gpio_hi_togl(uint32_t mask) { fake_gpio_hi_write(FAKE_GPIO_TOGL, mask); }

void pc_gpio_config_output(uint8_t pin);
void pc_gpio_release(uint8_t pin);
