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
 * Lock-free single-producer / single-consumer ring buffers for passing
 * fixed-size messages between the two RP2350 cores.
 *
 * Everything here is static inline so it is compiled into the caller's
 * section. That keeps the emulator core's use of these rings in SRAM (no
 * flash/XIP access) as long as the caller is itself RAM-resident.
 *
 * Rules:
 *   - Exactly one core may call push() on a given ring, and exactly one core
 *     may call pop()/peek(). (The producer and consumer may be the same core.)
 *   - Capacity must be a power of two. One slot is not wasted: head and tail
 *     are free-running counters and the fill level is (head - tail).
 *
 * Declare a ring type with EMU_RING_DECLARE(name, element_type, capacity),
 * which defines `name_t` and the functions name_init(), name_push(),
 * name_pop(), name_count() and name_is_empty().
 */

#pragma once

#include "hardware/sync.h" // __dmb()
#include <stdbool.h>
#include <stdint.h>

#define EMU_RING_DECLARE(name, elem_t, capacity)                                                   \
	_Static_assert(((capacity) & ((capacity) - 1)) == 0,                                       \
		       #name " capacity must be a power of two");                                  \
	typedef struct name##_t {                                                                  \
		volatile uint32_t head; /* written only by producer */                             \
		volatile uint32_t tail; /* written only by consumer */                             \
		elem_t slots[capacity];                                                            \
	} name##_t;                                                                                \
                                                                                                   \
	static inline void name##_init(name##_t *r)                                                \
	{                                                                                          \
		r->head = 0;                                                                       \
		r->tail = 0;                                                                       \
		__dmb();                                                                           \
	}                                                                                          \
                                                                                                   \
	static inline uint32_t name##_count(const name##_t *r)                                     \
	{                                                                                          \
		return r->head - r->tail;                                                          \
	}                                                                                          \
                                                                                                   \
	static inline bool name##_is_empty(const name##_t *r)                                      \
	{                                                                                          \
		return r->head == r->tail;                                                         \
	}                                                                                          \
                                                                                                   \
	/* Producer side. Returns false (and drops the element) if full. */                        \
	static inline bool name##_push(name##_t *r, const elem_t *e)                               \
	{                                                                                          \
		uint32_t h = r->head;                                                              \
		if (h - r->tail >= (capacity)) {                                                   \
			return false;                                                              \
		}                                                                                  \
		r->slots[h & ((capacity) - 1)] = *e;                                               \
		__dmb(); /* element visible before head moves */                                   \
		r->head = h + 1;                                                                   \
		return true;                                                                       \
	}                                                                                          \
                                                                                                   \
	/* Consumer side. Returns false if empty. */                                               \
	static inline bool name##_pop(name##_t *r, elem_t *e)                                      \
	{                                                                                          \
		uint32_t t = r->tail;                                                              \
		if (r->head == t) {                                                                \
			return false;                                                              \
		}                                                                                  \
		__dmb(); /* read element only after seeing head */                                 \
		*e = r->slots[t & ((capacity) - 1)];                                               \
		__dmb(); /* finish reading before releasing the slot */                            \
		r->tail = t + 1;                                                                   \
		return true;                                                                       \
	}
