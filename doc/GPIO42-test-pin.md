# GPIO42 E Clock Re-synchronization Test Pin

## Overview
GPIO42 has been configured as a test indicator to show when the E clock re-synchronization is happening. This helps with debugging and analyzing timing behavior of the emulator.

## Behavior
- **LOW (0)**: Normal operation - emulator is running at or behind real-time
- **HIGH (1)**: Re-synchronization active - emulator is ahead of real-time and waiting for hardware E clock to catch up

## Usage

### Enable/Disable at Build Time
The test pin can be easily enabled or disabled by editing `src/clock.h`:

```c
// Set to 1 to enable GPIO42 as a test indicator (goes high during re-sync)
// Set to 0 to disable for production builds
#define ECLOCK_RESYNC_TEST_PIN 1
```

**To disable:** Change `1` to `0` and rebuild:
```bash
make clean && make
```

### Where Re-synchronization Occurs
Re-synchronization happens in the `eclock_check_timing()` function when:
1. The emulated CPU has executed more cycles than real time
2. The cycle overage credit is insufficient to cover the difference
3. The emulator must wait for the hardware E clock to catch up

## Pin Location
- **GPIO42** is available on the NED_SYS7 board (48 GPIO RP2350)
- Can be monitored with an oscilloscope or logic analyzer
- Note: GPIO42 is not available on the BOARD_PICO2 (26 GPIO) but the code will compile safely with conditionals

## Implementation Details
- Initialization: `eclock_init()` sets up GPIO42 as output, starting LOW
- Set HIGH: Just before entering wait loop in `eclock_check_timing()`
- Set LOW: Immediately after wait loop completes
- Overhead: Minimal - only two `gpio_put()` calls when re-sync is needed
- Conditional compilation: All GPIO42 code is wrapped in `#if ECLOCK_RESYNC_TEST_PIN`

## Related Source Files
- `src/clock.h` - Feature enable/disable flag and GPIO definition
- `src/clock.c` - GPIO initialization and toggle logic
