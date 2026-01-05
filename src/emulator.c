#include "emulator.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include "usb_cdc.h"

#undef GET_NAME
#define GET_NAME(fsm)                             \
    do {                                          \
        fsm->current_state_name = __FUNCTION__;   \
        printf("Entering %s\r\n", fsm->current_state_name);  \
    } while (0)

static queue_t      sm_event_queue;      // Events => Emulator State Machine
static queue_t      notification_queue;  // Notifications => USB CDC console
static FSM          emulator_fsm;
static state_method paused_state;        // history

static void s_initializing(FSM *fsm, uint8_t event);
static void s_resetting(FSM *fsm, uint8_t event);
static void s_running(FSM *fsm, uint8_t event);
static void s_halted(FSM *fsm, uint8_t event);
static void s_paused(FSM *fsm, uint8_t event);
static void s_loading(FSM *fsm, uint8_t event);
static void s_waiting_for_interrupt(FSM *fsm, uint8_t event);

const char *sm_current_state_name(void) {
    return emulator_fsm.current_state_name ? emulator_fsm.current_state_name : "Unknown";
}

static bool __time_critical_func(receive_sm_event)(FSM *unused, uint8_t *event) {
    (void)unused;
    return queue_try_remove(&sm_event_queue, event);
}

bool __time_critical_func(post_sm_event)(sm_event_t event) {
    uint8_t ev = (uint8_t)event;
    return queue_try_add(&sm_event_queue, &ev);
}

bool __time_critical_func(receive_sm_notification)(sm_notification_t *notification) {
    uint8_t notif;
    if (queue_try_remove(&notification_queue, &notif)) {
        *notification = (sm_notification_t)notif;
        return true;
    }
    return false;
}

static inline void send_notification(sm_notification_t notification) {
    uint8_t notif = (uint8_t)notification;
    queue_try_add(&notification_queue, &notif);
}

static inline void notify_ok(void) {
    send_notification(NOTIF_OK);
}

static inline void notify_error(void) {
    send_notification(NOTIF_ERROR);
}

// Entered after power-on
static void s_initializing(FSM *fsm, uint8_t event) {
    GET_NAME(fsm);
    printf("initializing CPU\r\n");
    cpu_init();
    printf("initializing interrupts\r\n");
    interrupts_init();
    printf("reading CMOS to shadow\r\n");
    memory_read_cmos_from_bus();

    switch (event) {
    case EVT_INIT:
        fsm_change_state(fsm, &s_resetting);
        break;
    }
}

// Entered after initialization, and whenever the /RESET is asserted.
static void s_resetting(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_POLL:
        interrupt_t interrupt = interrupt_check();
        if (interrupt != INT_RESET) {
            fsm_change_state(fsm, &s_running);
        }
        sleep_ms(1);
        break;
    case EVT_ENTER:
        GET_NAME(fsm);
        interrupt_service_reset();
        eclock_stop();
        break;
    case EV_CMD_HALT:
        fsm_change_state(fsm, &s_halted);
        notify_ok();
        break;
    case EV_CMD_RUN:
        fsm_change_state(fsm, &s_running);
        notify_ok();
        break;
    }
}

// Normal emulator running instructions
static void s_running(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_POLL:
        interrupt_t interrupt = interrupt_check();
        if (interrupt == INT_RESET) {
            fsm_change_state(fsm, &s_resetting);
        }
        // Check for breakpoints before executing instruction
        if (cpu_check_breakpoint(cpu.pc) && !cpu.stopped_at_breakpoint) {
            // Breakpoint hit - halt CPU and set flag to skip check on next run
            cpu.stopped_at_breakpoint = true;
            usb_cdc_printf("CPU halted at breakpoint at PC=$%04X Prior PC=$%04X\r\n", cpu.pc,
                           cpu.last_opcode_address);
            fsm_change_state(fsm, &s_halted);
        } else {
            // Execute one instruction (cycle-accurate)
            bus_sync();
            instruction_execute();
            debug_spi_log();
            if (cpu.wai_state) {
                fsm_change_state(fsm, &s_waiting_for_interrupt);
            } else if (cpu.halted) {
                // unimplemented opcode or HCF
                fsm_change_state(fsm, &s_halted);
            }
        }
        break;

    case EVT_ENTER:
        GET_NAME(fsm);
        eclock_start();
        cpu.stopped_at_breakpoint = false;
        cpu.running = true;
        cpu.halted = false;
        break;

    case EVT_EXIT:
        cpu.running = false;
        break;

    case EV_PAUSE_EMULATOR:
        paused_state = &s_running;
        fsm_change_state(fsm, &s_paused);
        notify_ok();
        break;

    case EV_CMD_RUN:
        notify_ok();
        break;

    case EV_CMD_HALT:
        fsm_change_state(fsm, &s_halted);
        notify_ok();
        break;

    case EV_CMD_RESET:
        fsm_change_state(fsm, &s_resetting);
        notify_ok();
        break;

    case EV_CMD_LOAD:
        fsm_change_state(fsm, &s_loading);
        notify_ok();
        break;
    }
}

// Entered when a "halt" command has been issued, or upon a breakpoint
static void s_halted(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_ENTER:
        GET_NAME(fsm);
        eclock_stop();  // E clock OFF
        cpu.halted = true;
        break;
    case EVT_EXIT:
        cpu.halted = false;
        cpu.stopped_at_breakpoint = false;
        break;
    case EV_CMD_RUN:
        fsm_change_state(fsm, &s_running);
        notify_ok();
        break;
    case EV_PAUSE_EMULATOR:
    case EV_RESUME_EMULATOR:
    case EV_CMD_HALT:
        notify_ok();
        break;
    case EV_CMD_RESET:
        fsm_change_state(fsm, &s_resetting);
        notify_ok();
        break;
    case EV_CMD_LOAD:
        fsm_change_state(fsm, &s_loading);
        notify_ok();
        break;
    }
}

// Paused by the USB commands for synchronization
static void s_paused(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_ENTER:
        GET_NAME(fsm);
        break;
    case EVT_EXIT:
        break;
    case EV_RESUME_EMULATOR:
        if (paused_state) {
            fsm_change_state(fsm, paused_state);
            paused_state = NULL;
        }
        notify_ok();
        break;
    case EV_PAUSE_EMULATOR:
        notify_ok();
        break;
    }
}

// Paused by the USB commands during a new ROM image load
static void s_loading(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_ENTER:
        GET_NAME(fsm);
        eclock_stop();  // E clock OFF
        break;
    case EVT_EXIT:
        break;
    case EV_CMD_RESET:
        fsm_change_state(fsm, &s_resetting);
        notify_ok();
        break;
    }
}

// Entered upon a WAI instruction execution
static void s_waiting_for_interrupt(FSM *fsm, uint8_t event) {
    switch (event) {
    case EVT_ENTER:
        GET_NAME(fsm);
        break;
    case EVT_EXIT:
        break;
    case EVT_POLL:
        interrupt_t interrupt = interrupt_check();
        if (interrupt == INT_RESET) {
            fsm_change_state(fsm, &s_resetting);
        } else if (interrupt != INT_NONE) {
            fsm_change_state(fsm, &s_running);
        }
        break;
    }
}

bool run_emulator_sm(void) {
    queue_init(&sm_event_queue, 1, 16);
    queue_init(&notification_queue, 1, 16);
    emulator_fsm.receive_event = &receive_sm_event;
    return fsm_run(&emulator_fsm, &s_initializing);
}
