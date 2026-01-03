#include "emulator.h"
#include "clock.h"
#include "cpu_state.h"
#include "memory.h"
#include "bus.h"
#include "instructions.h"
#include "interrupts.h"
#include "debug_spi.h"

static queue_t sm_event_queue;     // Events => Emulator State Machine
static queue_t notification_queue; // Notifications => USB CDC console
static FSM emulator_fsm;

static void s_initializing(FSM *fsm, uint8_t event);
static void s_resetting(FSM *fsm, uint8_t event);
static void s_running(FSM *fsm, uint8_t event);
static void s_halted(FSM *fsm, uint8_t event);
static void s_paused(FSM *fsm, uint8_t event);
static void s_loading(FSM *fsm, uint8_t event);
static void s_waiting_for_interrupt(FSM *fsm, uint8_t event);

static bool receive_sm_event(FSM *unused, uint8_t *event)
{
    (void)unused;
    return queue_try_remove(&sm_event_queue, event);
}

bool post_sm_event(sm_event_t event)
{
    return queue_try_add(&sm_event_queue, &event);
}

bool receive_sm_notification(sm_notification_t *notification)
{
    return queue_try_remove(&notification_queue, notification);
}

static inline void send_notification(sm_notification_t notification)
{
    queue_try_add(&notification_queue, &notification);
}

static inline void notify_ok(void)
{
    send_notification(NOTIF_OK);
}

static inline void notify_error(void)
{
    send_notification(NOTIF_ERROR);
}

// Entered after power-on
static void s_initializing(FSM *fsm, uint8_t event)
{
    cpu_init();
    interrupts_init();

    switch (event)
    {
        case EVT_INIT:
            fsm_change_state(fsm, &s_resetting);
            break;
    }
}

// Entered after initialization, and whenever the /RESET is asserted.
static void s_resetting(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_POLL:
            interrupt_t interrupt = interrupt_check();
            if (interrupt != INT_RESET)
            {
                fsm_change_state(fsm, &s_running);
            }
            break;
        case EVT_ENTER:
        case EVT_EXIT:
    }
}

// Normal emulator running instructions
static void s_running(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_ENTER:
            // turn E clock on
            break;
        case EVT_EXIT:
            break;
        case EVT_POLL:
            interrupt_t interrupt = interrupt_check();
            if (interrupt == INT_RESET)
            {
                fsm_change_state(fsm, &s_resetting);
            }
            break;
    }
}

// Entered when a "halt" command has been issued, or upon a breakpoint
static void s_halted(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_ENTER:
            // E clock OFF
            break;
        case EVT_EXIT:
            break;
    }
}

// Paused by the USB commands for synchronization
static void s_paused(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_ENTER:
            break;
        case EVT_EXIT:
            break;
    }
}

// Paused by the USB commands during a new ROM image load
static void s_loading(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_ENTER:
            // E clock OFF
            break;
        case EVT_EXIT:
            break;
    }
}

// Entered upon a WAI instruction execution
static void s_waiting_for_interrupt(FSM *fsm, uint8_t event)
{
    switch (event)
    {
        case EVT_ENTER:
            break;
        case EVT_EXIT:
            break;
        case EVT_POLL:
            interrupt_t interrupt = interrupt_check();
            if (interrupt == INT_RESET)
            {
                fsm_change_state(fsm, &s_resetting);
            }
            else if (interrupt != INT_NONE)
            {
                fsm_change_state(fsm, &s_running);
            }
            break;
    }
}

bool run_emulator_sm(void)
{
    queue_init(&sm_event_queue, sizeof(sm_event_t), 16);
    queue_init(&notification_queue, sizeof(sm_notification_t), 16);
    emulator_fsm.receive_event = &receive_sm_event;
    return fsm_run(&emulator_fsm, &s_initializing);
}