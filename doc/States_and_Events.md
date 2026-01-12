# Events

## USB commands

- run
- halt
- reset
- clear counts
- load
- resume from breakpoint
- `cpu.pc` set

## Runtime events

- Power-on
- `/RESET` asserted
- `/RESET` released
- `/IRQ` edge
- `/NMI` edge
- Breakpoint hit
- bad opcode
- `WAI` instruction

## Global State

- eclock running
- instructions executing
  - halted: no instructions
- held in reset
- in `WAI` state
- loading new program
- 5V lost: `/IRQ`, `/NMI`, `/RESET` all asserted

## Global Counters

- instruction count (from instruction execution)
- simulated cycle count (from instruction execution)
- Eclock count (from PIO)
- over/under cycles (from instruction execution)
- Per-instruction count and cycles (can be turned off)

## NOTES

- WAI instruction shortens IRQ response to 4 cycles
  - load vector (2 cycles)
- IRQ response is nornally 12 cycles
- WAI takes 9 cycles (stacks PC, X, A, B, CCR)

-------

```mermaid
stateDiagram-v2

[*] --> Initializing

Initializing: Initializing
Initializing:entry/fingerprint system
Initializing:entry/read RAM from bus to shadow
Initializing:entry/initialize CPU

Resetting: Resetting
Resetting: entry/reset CPU
Resetting: entry/stop E clock

Running: Running
Running: entry/start E clock
Running: do/bus sync, execute instruction

Halted: Halted
Halted: entry/stop E clock

Paused: Paused

Loading: Loading
Loading: entry/stop E clock

WaitingForInterrupt: WaitingForInterrupt

Initializing --> Resetting : EVT_INIT
Running --> Resetting : EVT_POLL[interrupt == INT_RESET]

Resetting --> Running : EVT_POLL[!interrupt == INT_RESET]
Resetting --> Halted : EV_CMD_HALT
Resetting --> Running : EV_CMD_RUN
Resetting --> Resetting : EVT_POLL[interrupt == INT_RESET]

Running --> Halted : EVT_POLL[breakpoint hit]
Running --> WaitingForInterrupt : EVT_POLL[cpu.wai_state]
Running --> Halted : EVT_POLL[cpu.halted]
Running --> Paused : EV_PAUSE_EMULATOR
Running --> Halted : EV_CMD_HALT
Running --> Resetting : EV_CMD_RESET
Running --> Loading : EV_CMD_LOAD

Halted --> Running : EV_CMD_RUN
Halted --> Resetting : EV_CMD_RESET
Halted --> Loading : EV_CMD_LOAD

Paused --> Running : EV_RESUME_EMULATOR
Paused --> Halted : EV_RESUME_EMULATOR
Paused --> Resetting : EV_RESUME_EMULATOR
Paused --> Loading : EV_RESUME_EMULATOR
Paused --> WaitingForInterrupt : EV_RESUME_EMULATOR

Loading --> Resetting : EV_CMD_RESET

WaitingForInterrupt --> Resetting : EVT_POLL[interrupt == INT_RESET]
WaitingForInterrupt --> Running : EVT_POLL[interrupt != INT_NONE]



```
