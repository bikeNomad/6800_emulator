#include "simple_fsm.h"
#include "pico.h"

static inline void fsm_dispatch(FSM *fsm, uint8_t event) { fsm->current_state(fsm, event); }

bool fsm_init(FSM *fsm, state_method initial_state) {
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

static inline void fsm_handle_event(FSM *fsm, uint8_t event) {
    fsm_dispatch(fsm, event);
    if (fsm->next_state != NULL) {
        fsm_dispatch(fsm, EVT_EXIT);
        fsm->current_state = fsm->next_state;
        fsm->next_state = NULL;
        fsm_dispatch(fsm, EVT_ENTER);
    }
}

bool __time_critical_func(fsm_run)(FSM *fsm, state_method initial_state) {
    if (!fsm_init(fsm, initial_state)) {
        return false;
    }

    while (fsm->current_state != NULL) {
        uint8_t event;
        while (fsm->receive_event(fsm, &event)) {
            fsm->last_event_received = event;
            fsm_handle_event(fsm, event);
        }
        fsm_handle_event(fsm, EVT_POLL);
    }

    return true;
}