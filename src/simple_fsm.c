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

#include "simple_fsm.h"
#include "pico.h"

static inline void fsm_dispatch(FSM *fsm, uint8_t event)
{
	fsm->current_state(fsm, event);
}

bool fsm_init(FSM *fsm, state_method initial_state)
{
	fsm->last_event_received = EVT_INIT;
	fsm->current_state = initial_state;
	fsm->next_state = NULL;
	fsm_dispatch(fsm, EVT_INIT);
	if (fsm->next_state == NULL) {
		return false;
	}
	fsm->current_state = fsm->next_state;
	fsm->next_state = NULL;
	fsm_dispatch(fsm, EVT_ENTER);
	return true;
}

static inline void fsm_handle_event(FSM *fsm, uint8_t event)
{
	fsm_dispatch(fsm, event);
	state_method next_state = fsm->next_state;
	if (next_state != NULL) {
		fsm->next_state = NULL;
		fsm_dispatch(fsm, EVT_EXIT);
		fsm->current_state = next_state;
		fsm_dispatch(fsm, EVT_ENTER);
	}
}

bool __time_critical_func(fsm_run)(FSM *fsm, state_method initial_state)
{
	if (!fsm_init(fsm, initial_state)) {
		return false;
	}

	while (fsm->current_state != NULL) {
		uint8_t event;
		if (fsm->receive_event(fsm, &event)) {
			fsm->last_event_received = event;
			fsm_handle_event(fsm, event);
		}
		fsm_handle_event(fsm, EVT_POLL);
	}

	return true;
}