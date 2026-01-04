#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Simple flat state machine executive in which each state is represented by a separate
// function that receives a uint8_t "event_type" argument. These events include the 
// following standard events:
// EVT_ENTER: entry to the state
// EVT_EXIT: exit from the state
// EVT_INIT: initialization of the state machine (must do a transition)
// EVT_POLL: called during state
// EVT_TIMEOUT: timer timeout

typedef enum fsm_standard_event_t {
    EVT_INIT,
    EVT_ENTER,
    EVT_EXIT,
    EVT_POLL,
    // EVT_TIMEOUT, // TODO
    EVT_USER_START  // start derived SM events here
} fsm_standard_event_t;

struct FSM;
typedef struct FSM FSM;

// Signature of state event handlers
typedef void (*state_method)(FSM *fsm, uint8_t event_type);

// Signature of method to dequeue events for the state machine.
// Returns false if there are no events available.
typedef bool (*rx_event_t)(FSM* fsm, uint8_t *event_type);

struct FSM {
    state_method current_state;
    state_method next_state;
    rx_event_t receive_event;
    uint8_t last_event_received;
    char const * current_state_name;  // set by GET_NAME(fsm) macro
    // timer_expires TODO 
    // poll_interval TODO
    // entered_state TODO
    // timer TODO
};

bool fsm_run(FSM* fsm, state_method initial_state);

static inline void fsm_change_state(FSM* fsm, state_method new_state) {
    if (new_state != fsm->current_state) {
        fsm->next_state = new_state;
    }
}

static inline void fsm_terminate(FSM* fsm) {
    fsm->current_state = NULL;
}

#define GET_NAME(fsm) do { fsm->current_state_name = __FUNCTION__; } while (0)
