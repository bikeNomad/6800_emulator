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

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Simple flat state machine executive in which each state is represented by a
// separate function that receives a uint8_t "event_type" argument. These events
// include the following standard events:
// EVT_INIT: initialization of the state machine (must do a transition)
// EVT_ENTER: entry to the state
// EVT_EXIT: exit from the state
// EVT_POLL: called during state

typedef enum fsm_standard_event_t {
	EVT_INIT,
	EVT_ENTER,
	EVT_EXIT,
	EVT_POLL,
	EVT_USER_START // start derived SM events here
} fsm_standard_event_t;

struct FSM;
typedef struct FSM FSM;

// Signature of state event handlers
typedef void (*state_method)(FSM *fsm, uint8_t event_type);

// Signature of method to dequeue events for the state machine.
// Returns false if there are no events available.
typedef bool (*rx_event_t)(FSM *fsm, uint8_t *event_type);

struct FSM {
	state_method current_state;
	state_method next_state;
	rx_event_t receive_event;
	uint8_t last_event_received;
	char const *current_state_name; // set by GET_NAME(fsm) macro
};

bool fsm_run(FSM *fsm, state_method initial_state);

static inline void fsm_change_state(FSM *fsm, state_method new_state)
{
	if (new_state != fsm->current_state) {
		fsm->next_state = new_state;
	}
}

static inline void fsm_terminate(FSM *fsm)
{
	fsm->current_state = NULL;
}

#define GET_NAME(fsm)                                                                              \
	do {                                                                                       \
		fsm->current_state_name = __FUNCTION__;                                            \
	} while (0)
