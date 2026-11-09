/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * GPIO access for PC trigger outputs (GPIO 32-47, the SIO "hi" bank).
 *
 * The pc_gpio_hi_*() writes are used on the emulator core and are plain SIO
 * register writes (no SDK calls, which live in flash). Pin setup is done on
 * the host core with the SDK. tests/host/fakes has a stand-in for this file.
 */

#pragma once

#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico.h"
#include <stdint.h>

// mask bit n = GPIO 32+n
static __force_inline void pc_gpio_hi_set(uint32_t mask)
{
	sio_hw->gpio_hi_set = mask;
}

static __force_inline void pc_gpio_hi_clr(uint32_t mask)
{
	sio_hw->gpio_hi_clr = mask;
}

static __force_inline void pc_gpio_hi_togl(uint32_t mask)
{
	sio_hw->gpio_hi_togl = mask;
}

// Host core: make pin a SIO output driving low.
static inline void pc_gpio_config_output(uint8_t pin)
{
	gpio_init(pin);
	gpio_put(pin, false);
	gpio_set_dir(pin, GPIO_OUT);
}

// Host core: no action uses pin any more; drive it low and leave it an output.
static inline void pc_gpio_release(uint8_t pin)
{
	gpio_put(pin, false);
}
