<!--
SPDX-License-Identifier: MIT

Copyright 2026 Ned Konz <ned@metamagix.tech>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-->

# PSRAM Implementation Plan for RP2350

## Overview

This document provides a comprehensive plan for implementing QSPI PSRAM support (APS6404L-3SQR-SN) on the RP2350 using GPIO47 as the secondary chip select (CS1) on the QMI (QSPI Memory Interface) bus.

**Target Hardware:**

- **MCU:** RP2350 (Raspberry Pi Pico 2 / Ned's System 7 Board)
- **PSRAM:** APS6404L-3SQR-SN (8MB QSPI PSRAM by APMemory)
- **Interface:** QMI (shared with flash on CS0)
- **Chip Select:** GPIO47 (QMI_SS1)

**Current Status:** The existing implementation in `src/psram.c` has several critical issues that prevent proper PSRAM operation.

---

## Table of Contents

1. Current Issues
2. RP2350 QMI Overview
3. GPIO47 Configuration
4. Implementation Options
5. APS6404L PSRAM Details
6. Recommended Implementation Plan
7. Code Examples
8. Testing Strategy
9. References

---

## 1. Current Issues

### Problems in `src/psram.c`

#### Issue #1: Incorrect GPIO Configuration

```c
// WRONG: Treating GPIO47 as a regular GPIO pin
gpio_init(GPIO_PSRAM_CS);
gpio_set_dir(GPIO_PSRAM_CS, GPIO_OUT);
gpio_put(GPIO_PSRAM_CS, 1);
```

---

## 3. GPIO47 Configuration

### Correct GPIO Function Setup

GPIO47 must be configured as a QMI function, not as a regular GPIO:

```c
#include "hardware/gpio.h"

// CORRECT: Configure GPIO47 for QMI CS1 function
gpio_set_function(GPIO_PSRAM_CS, GPIO_FUNC_XIP_CS1);
```

**Important Notes:**

- `GPIO_FUNC_XIP_CS1` is the correct function for QMI chip select 1
- Do NOT use `gpio_init()`, `gpio_set_dir()`, or `gpio_put()` on GPIO47
- The QMI peripheral will control the CS1 signal automatically
- No pull-up/pull-down needed - the QMI handles this

### Pin Verification

Verify GPIO47 is available and not conflicting with other peripherals:

```c
// GPIO47 is only available on RP2350B variant (48 GPIO package)
#if defined(PICO_RP2350B) || (BOARD_TYPE == BOARD_NED_SYS7)
    #define GPIO_PSRAM_CS 47  // Safe to use
#else
    #error "GPIO47 not available on this RP2350 variant"
#endif
```

### Shared QSPI Bus Considerations

Since PSRAM shares the QSPI bus with flash (CS0):

- **SCLK, SD0-SD3** are shared between flash and PSRAM
- Only the chip select (CS0 vs CS1) distinguishes them
- QMI handles bus arbitration automatically
- Both devices must tolerate the same voltage (3.3V)

---

## 4. Implementation Options

There are two approaches to implement PSRAM support on the RP2350 QMI:

### Option A: QMI Direct Mode (Recommended)

**Description:** Use the QMI's direct/manual mode with TX/RX FIFOs to send individual commands.

#### Advantages

✅ **Flexible:** Works with any QSPI device without pre-configuration  
✅ **No hard-coded timing:** Adjust parameters dynamically  
✅ **Full control:** Can send arbitrary commands (reset, ID, mode changes)  
✅ **Easier debugging:** See exact commands sent/received  
✅ **Universal:** Same code works across different PSRAM chips  

#### Disadvantages

❌ **Slower:** Each transaction requires FIFO management  
❌ **CPU overhead:** Not memory-mapped, requires function calls  
❌ **More code:** Need to handle FIFOs and status flags  

#### When to Use

- Initial development and testing
- When you need to support multiple PSRAM types
- When memory access speed isn't critical (<10 MB/s is acceptable)
- For command-based operations (ID read, reset, configuration)

#### Basic Structure

```c
// 1. Enable QMI direct mode with CS1
qmi_hw->direct_csr = QMI_DIRECT_CSR_EN_BITS | 
                     QMI_DIRECT_CSR_AUTO_CS1N_BITS;

// 2. Send command via TX FIFO
qmi_hw->direct_tx = command_byte;

// 3. Send address (if needed)
qmi_hw->direct_tx = address_byte;

// 4. Read response from RX FIFO
uint8_t response = qmi_hw->direct_rx;

// 5. Disable direct mode
qmi_hw->direct_csr = 0;
```

---

### Option B: QMI XIP Mode (Memory-Mapped)

**Description:** Configure memory window M1 to memory-map PSRAM into the address space.

#### Advantages

✅ **Fast:** Direct memory access, no function calls  
✅ **Simple usage:** Read/write like normal memory  
✅ **DMA compatible:** Can use DMA for transfers  
✅ **Efficient:** Hardware handles all timing automatically  

#### Disadvantages

❌ **Rigid:** Requires pre-configuration of timing and commands  
❌ **Less flexible:** Hard to switch between PSRAM types  
❌ **Initial complexity:** Must configure all timing parameters correctly  
❌ **Limited commands:** Only supports read/write, not special commands  

#### When to Use

- Production code after development is complete
- When you need maximum performance (>20 MB/s)
- When PSRAM access patterns are simple (mostly sequential reads)
- When the PSRAM type is fixed and well-characterized

#### Basic Structure

```c
// Configure M1 memory window for PSRAM on CS1
qmi_hw->m[1].timing = (CLKDIV << QMI_M1_TIMING_CLKDIV_LSB) |
                      (RXDELAY << QMI_M1_TIMING_RXDELAY_LSB);

qmi_hw->m[1].rcmd = PSRAM_CMD_READ;  // Read command
qmi_hw->m[1].wcmd = PSRAM_CMD_WRITE; // Write command

qmi_hw->m[1].rfmt = /* read format bits */;
qmi_hw->m[1].wfmt = /* write format bits */;

// PSRAM now memory-mapped at 0x11000000 - 0x117FFFFF (8MB window)
volatile uint8_t *psram = (volatile uint8_t *)0x11000000;
psram[0x12345] = 0x42;  // Direct write
uint8_t data = psram[0x12345];  // Direct read
```

---

### Recommended Approach

**Start with Option A (Direct Mode):**

1. Implement basic functionality using Direct Mode
2. Test read ID, reset, and basic read/write operations  
3. Verify timing and command sequences work correctly

**Optionally migrate to Option B (XIP Mode) later:**

1. Once Direct Mode works, capture the working timing parameters
2. Configure M1 memory window with those parameters
3. Test memory-mapped access
4. Keep Direct Mode functions for initialization and special commands

This hybrid approach gives you the best of both worlds:

- Direct Mode for initialization, ID reading, and special operations
- XIP Mode for fast bulk data transfers (if needed)

**Problem:** GPIO47 is a dedicated QMI chip select pin (CS1). Using standard GPIO functions bypasses the QMI hardware and prevents proper QSPI operation.

**Impact:** The PSRAM chip select won't be synchronized with QSPI transactions, and the QMI hardware won't control it.

#### Issue #2: No QMI Peripheral Configuration

```c
#include "hardware/structs/qmi.h"  // Included but never used
```

**Problem:** The code includes the QMI header but never configures the QMI peripheral. Without QMI configuration, no QSPI communication can occur.

**Impact:** All read/write operations are placeholders that don't actually communicate with the PSRAM.

#### Issue #3: Placeholder Implementations

```c
uint16_t psram_get_manufacturer_id(void) {
    // TODO: Implement actual QSPI communication with APS6404L
    if (psram_initialized) {
        return PSRAM_MANUFACTURER_ID;  // Returns hardcoded value!
    }
    return 0x00;
}
```

**Problem:** Functions return hardcoded values instead of reading from the actual PSRAM chip.

**Impact:** The code appears to work but doesn't verify the PSRAM is present or functioning.

#### Issue #4: No Actual QSPI Transactions

```c
bool psram_read(uint32_t address, uint8_t *data, uint32_t length) {
    // ...validation...
    
    // Note: This is a simplified implementation
    // In practice, you'd use the QMI to perform the actual QSPI transaction
    
    memset(data, 0xAA, length);  // Returns dummy data!
    return true;
}
```

**Problem:** Read/write functions don't perform any QSPI communication with the PSRAM.

**Impact:** Memory operations fail silently and return garbage data.

---

## 2. RP2350 QMI Overview

### What is QMI?

The **QMI (QSPI Memory Interface)** is the RP2350's hardware peripheral for interfacing with QSPI flash and PSRAM. It replaces the SSI peripheral used in RP2040.

### Key Features

- **Two Memory Windows (M0 and M1):** Independent configurations for two devices
  - M0 maps to CS0 (typically flash)
  - M1 maps to CS1 (GPIO47 - for PSRAM)

- **Flexible Transfer Formats:**
  - Single, Dual, or Quad SPI
  - Configurable command, address, and data phases
  - Support for dummy cycles
  - DTR (Double Transfer Rate) mode

- **Two Operating Modes:**
  1. **XIP Mode (Memory-Mapped):** Direct memory access through address windows
  2. **Direct Mode:** Manual FIFO-based SPI transactions

- **Independent Timing:** Each memory window has its own:
  - Clock divisor
  - RX sampling delay
  - Chip select timing (setup, hold, min deselect)

### QMI Hardware Structure

```
┌─────────────────────────────────────────┐
│            QMI Peripheral               │
│                                         │
│  ┌─────────────┐      ┌─────────────┐ │
│  │  Memory M0  │      │  Memory M1  │ │
│  │   (CS0)     │      │   (CS1)     │ │
│  │   Flash     │      │   PSRAM     │ │
│  └─────────────┘      └─────────────┘ │
│                                         │
│  ┌─────────────────────────────────┐   │
│  │      Direct Mode (Manual)       │   │
│  │   TX FIFO / RX FIFO / Control   │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
         │                    │
         ▼                    ▼
    QSPI Pins            GPIO47 (CS1)
    (SD0-3, SCLK)
```

---

## 5. APS6404L PSRAM Details

### Device Specifications

**APMemory APS6404L-3SQR-SN:**

- **Capacity:** 8 Megabytes (64 Megabits)
- **Organization:** 8M x 8 bits
- **Interface:** QSPI (Quad SPI)
- **Voltage:** 3.0V - 3.6V (compatible with 3.3V)
- **Speed:** Up to 133 MHz clock (84 MHz typical)
- **Package:** 8-pin SOP

### Command Set

The APS6404L uses standard SPI/QSPI commands:

| Command | Hex Code | Description | Address | Dummy Cycles | Data |
|---------|----------|-------------|---------|--------------|------|
| **Read** | `0x03` | Standard read (SPI mode) | 3 bytes | 0 | N bytes |
| **Fast Read** | `0x0B` | Fast read (SPI mode) | 3 bytes | 1 byte | N bytes |
| **Fast Read Quad** | `0xEB` | Fast read (Quad mode) | 3 bytes | 6 cycles | N bytes |
| **Write** | `0x02` | Standard write (SPI mode) | 3 bytes | 0 | N bytes |
| **Quad Write** | `0x38` | Quad write | 3 bytes | 0 | N bytes |
| **Enter Quad Mode** | `0x35` | Enable Quad I/O | None | 0 | None |
| **Exit Quad Mode** | `0xF5` | Disable Quad I/O | None | 0 | None |
| **Reset Enable** | `0x66` | Enable reset | None | 0 | None |
| **Reset** | `0x99` | Perform reset | None | 0 | None |
| **Read ID** | `0x9F` | Read manufacturer/device ID | 3 bytes | 0 | 3 bytes |

**Note:** The APS6404L doesn't have a separate "Read ID" command like flash. Instead, reading from address `0x000000` with command `0x9F` returns:

- Byte 0: Manufacturer ID (`0x0D` for APMemory)
- Byte 1: KGD (Known Good Die) - typically `0x5D`
- Byte 2: EID (Electronic ID) - device specific

### Timing Parameters (Typical at 3.3V)

- **Clock Frequency:** Up to 133 MHz (recommend starting at 50-66 MHz)
- **Chip Select Setup Time:** 5 ns minimum
- **Chip Select Hold Time:** 5 ns minimum
- **Command to Data Delay:** Varies by command (see dummy cycles)
- **Power-up Time:** 150 μs maximum

### Memory Organization

```
Address Range:  0x000000 - 0x7FFFFF (8MB)

Linear addressing, no pages or sectors.
Random access to any byte at any time.
No erase cycles required (unlike flash).
```

### Initialization Sequence

For reliable operation with the APS6404L:

1. **Power-up delay:** Wait ≥200 μs after power applied
2. **Reset sequence:**
   - Send Reset Enable command (`0x66`)
   - Send Reset command (`0x99`)
   - Wait 50 μs
3. **Verify chip presence:** Read ID at address `0x000000` with `0x9F` command
4. **Optional:** Enter Quad mode with `0x35` command for faster transfers

### SPI vs. Quad SPI Mode

The APS6404L supports both SPI and Quad SPI:

**SPI Mode (Default after reset):**

- Uses SD0 (MOSI) and SD1 (MISO) only
- Commands: `0x03` (Read), `0x0B` (Fast Read), `0x02` (Write)
- Slower but more compatible

**Quad SPI Mode (After `0x35` command):**

- Uses all 4 data lines (SD0-SD3) simultaneously
- Commands: `0xEB` (Fast Read Quad), `0x38` (Quad Write)
- 4x faster data transfer
- Recommended for production use

### Recommended Clock Divisor

For RP2350 running at typical system clock speeds:

| System Clock | Divisor | QSPI Clock | Safe? |
|--------------|---------|------------|-------|
| 150 MHz | 2 | 75 MHz | ✅ Yes |
| 150 MHz | 3 | 50 MHz | ✅ Yes (conservative) |
| 150 MHz | 4 | 37.5 MHz | ✅ Yes (very safe) |
| 300 MHz | 4 | 75 MHz | ✅ Yes |
| 300 MHz | 6 | 50 MHz | ✅ Yes (conservative) |

**Recommendation:** Start with divisor=6 (50 MHz) for development, then optimize to divisor=4 or lower after testing.

---

## 6. Recommended Implementation Plan

This section provides a step-by-step plan for fixing the PSRAM implementation using **QMI Direct Mode** (Option A).

### Step 1: Update Includes and Defines

**File: `src/psram.c`**

Add the necessary headers:

```c
#include "psram.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>
```

Add configuration defines:

```c
// QMI Direct Mode clock divisor (system clock / divisor = QSPI clock)
#define PSRAM_CLKDIV 6  // Conservative: 50 MHz for 300 MHz system clock

// FIFO wait timeout (iterations)
#define FIFO_TIMEOUT 10000
```

### Step 2: Configure GPIO47

**File: `src/psram.c` → `psram_init()` function**

Replace the incorrect GPIO configuration:

```c
bool psram_init(void) {
    if (psram_initialized) {
        return true;
    }

    // Configure GPIO47 as QMI CS1 (not regular GPIO!)
    gpio_set_function(GPIO_PSRAM_CS, GPIO_FUNC_XIP_CS1);
    
    // Small delay after GPIO configuration
    busy_wait_us(10);
    
    // Continue with reset and ID check...
```

### Step 3: Implement QMI Direct Mode Helper Functions

**File: `src/psram.c`**

Add helper functions for QMI direct mode operations:

```c
// Enable QMI direct mode with CS1
static inline void qmi_direct_mode_enter(void) {
    // Configure direct mode: enable + use CS1 + set clock divisor
    qmi_hw->direct_csr = 
        QMI_DIRECT_CSR_EN_BITS |           // Enable direct mode
        QMI_DIRECT_CSR_AUTO_CS1N_BITS |    // Auto-control CS1
        (PSRAM_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB);  // Clock divisor
}

// Disable QMI direct mode
static inline void qmi_direct_mode_exit(void) {
    qmi_hw->direct_csr = 0;
}

// Wait for TX FIFO to be not full
static bool qmi_wait_tx_ready(void) {
    for (int i = 0; i < FIFO_TIMEOUT; i++) {
        if (!(qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS)) {
            return true;
        }
    }
    return false;  // Timeout
}

// Wait for RX FIFO to be not empty
static bool qmi_wait_rx_ready(void) {
    for (int i = 0; i < FIFO_TIMEOUT; i++) {
        if (!(qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            return true;
        }
    }
    return false;  // Timeout
}

// Send a byte via TX FIFO
static bool qmi_tx_byte(uint8_t data) {
    if (!qmi_wait_tx_ready()) {
        return false;
    }
    qmi_hw->direct_tx = data;
    return true;
}

// Receive a byte from RX FIFO
static bool qmi_rx_byte(uint8_t *data) {
    if (!qmi_wait_rx_ready()) {
        return false;
    }
    *data = (uint8_t)(qmi_hw->direct_rx & 0xFF);
    return true;
}
```

### Step 4: Implement PSRAM Reset

**File: `src/psram.c`**

Add a function to reset the PSRAM chip:

```c
static bool psram_reset(void) {
    qmi_direct_mode_enter();
    
    // Send Reset Enable command (0x66)
    if (!qmi_tx_byte(PSRAM_CMD_RESET_ENABLE)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Small delay between commands
    busy_wait_us(1);
    
    // Send Reset command (0x99)
    if (!qmi_tx_byte(PSRAM_CMD_RESET)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    qmi_direct_mode_exit();
    
    // Wait for reset to complete (APS6404L: ~50 μs)
    busy_wait_us(100);
    
    return true;
}
```

**Note:** Add these command defines to `src/psram.h`:

```c
#define PSRAM_CMD_RESET_ENABLE 0x66  // Reset enable
#define PSRAM_CMD_RESET        0x99  // Reset
```

### Step 5: Implement Read ID

**File: `src/psram.c`**

Replace the placeholder ID functions with real implementations:

```c
// Read manufacturer and device ID
static bool psram_read_id(uint8_t *mfg_id, uint8_t *device_id) {
    qmi_direct_mode_enter();
    
    // Send Read ID command (0x9F)
    if (!qmi_tx_byte(PSRAM_CMD_READ_ID)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Send 3-byte address (0x000000)
    if (!qmi_tx_byte(0x00) || !qmi_tx_byte(0x00) || !qmi_tx_byte(0x00)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Read 3 bytes: Manufacturer ID, KGD, EID
    uint8_t id_bytes[3];
    for (int i = 0; i < 3; i++) {
        if (!qmi_rx_byte(&id_bytes[i])) {
            qmi_direct_mode_exit();
            return false;
        }
    }
    
    qmi_direct_mode_exit();
    
    *mfg_id = id_bytes[0];      // Manufacturer ID (0x0D)
    *device_id = id_bytes[1];   // KGD (0x5D for APS6404L)
    
    return true;
}

uint16_t psram_get_manufacturer_id(void) {
    if (!psram_initialized) {
        return 0x00;
    }
    
    uint8_t mfg_id, dev_id;
    if (psram_read_id(&mfg_id, &dev_id)) {
        return mfg_id;
    }
    return 0x00;
}

uint16_t psram_get_device_id(void) {
    if (!psram_initialized) {
        return 0x00;
    }
    
    uint8_t mfg_id, dev_id;
    if (psram_read_id(&mfg_id, &dev_id)) {
        return dev_id;
    }
    return 0x00;
}
```

### Step 6: Update Initialization Sequence

**File: `src/psram.c` → `psram_init()` function**

Replace with proper initialization:

```c
bool psram_init(void) {
    if (psram_initialized) {
        return true;
    }

    printf("PSRAM: Initializing APS6404L on QMI CS1 (GPIO%d)...\n", GPIO_PSRAM_CS);
    
    // Step 1: Configure GPIO47 for QMI CS1
    gpio_set_function(GPIO_PSRAM_CS, GPIO_FUNC_XIP_CS1);
    busy_wait_us(10);
    
    // Step 2: Reset PSRAM
    if (!psram_reset()) {
        printf("PSRAM: Reset failed\n");
        return false;
    }
    printf("PSRAM: Reset complete\n");
    
    // Step 3: Read and verify ID
    uint8_t mfg_id, dev_id;
    if (!psram_read_id(&mfg_id, &dev_id)) {
        printf("PSRAM: Failed to read ID\n");
        return false;
    }
    
    printf("PSRAM: ID read - Manufacturer: 0x%02X, Device: 0x%02X\n", mfg_id, dev_id);
    
    // Step 4: Verify expected IDs
    if (mfg_id != PSRAM_MANUFACTURER_ID || dev_id != PSRAM_DEVICE_ID) {
        printf("PSRAM: Unexpected ID (expected 0x%02X:0x%02X)\n", 
               PSRAM_MANUFACTURER_ID, PSRAM_DEVICE_ID);
        return false;
    }
    
    printf("PSRAM: APS6404L detected and ready\n");
    psram_initialized = true;
    return true;
}
```

### Step 7: Implement Read Function

**File: `src/psram.c`**

Replace the placeholder read function with QMI direct mode implementation:

```c
bool psram_read(uint32_t address, uint8_t *data, uint32_t length) {
    if (!psram_initialized || address >= PSRAM_SIZE_BYTES ||
        (address + length) > PSRAM_SIZE_BYTES || data == NULL) {
        return false;
    }
    
    qmi_direct_mode_enter();
    
    // Send Fast Read command (0x0B)
    if (!qmi_tx_byte(PSRAM_CMD_READ)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Send 24-bit address (big-endian)
    if (!qmi_tx_byte((address >> 16) & 0xFF) ||
        !qmi_tx_byte((address >> 8) & 0xFF) ||
        !qmi_tx_byte(address & 0xFF)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // For Fast Read (0x0B), send 1 dummy byte
    if (!qmi_tx_byte(0x00)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Read data bytes
    for (uint32_t i = 0; i < length; i++) {
        if (!qmi_rx_byte(&data[i])) {
            qmi_direct_mode_exit();
            return false;
        }
    }
    
    qmi_direct_mode_exit();
    return true;
}
```

### Step 8: Implement Write Function

**File: `src/psram.c`**

Replace the placeholder write function:

```c
bool psram_write(uint32_t address, const uint8_t *data, uint32_t length) {
    if (!psram_initialized || address >= PSRAM_SIZE_BYTES ||
        (address + length) > PSRAM_SIZE_BYTES || data == NULL) {
        return false;
    }
    
    qmi_direct_mode_enter();
    
    // Send Write command (0x02)
    if (!qmi_tx_byte(PSRAM_CMD_WRITE)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Send 24-bit address (big-endian)
    if (!qmi_tx_byte((address >> 16) & 0xFF) ||
        !qmi_tx_byte((address >> 8) & 0xFF) ||
        !qmi_tx_byte(address & 0xFF)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    // Write data bytes
    for (uint32_t i = 0; i < length; i++) {
        if (!qmi_tx_byte(data[i])) {
            qmi_direct_mode_exit();
            return false;
        }
    }
    
    qmi_direct_mode_exit();
    return true;
}
```

### Step 9: Update Basic Test

**File: `src/psram.c` → `psram_test_basic()` function**

```c
bool psram_test_basic(void) {
    if (!psram_initialized) {
        return false;
    }
    
    // Read ID and verify
    uint8_t mfg_id, dev_id;
    if (!psram_read_id(&mfg_id, &dev_id)) {
        printf("PSRAM: Basic test failed - cannot read ID\n");
        return false;
    }
    
    if (mfg_id == PSRAM_MANUFACTURER_ID && dev_id == PSRAM_DEVICE_ID) {
        return true;
    }
    
    printf("PSRAM: Basic test failed - wrong ID (0x%02X:0x%02X)\n", mfg_id, dev_id);
    return false;
}
```

### Step 10: Compile and Test

1. **Clean and rebuild:**

   ```bash
   make clean
   make
   ```

2. **Flash to device:**

   ```bash
   # Copy .uf2 file to device or use picotool
   ```

3. **Monitor serial output:**

   ```bash
   # Check for PSRAM initialization messages
   # Should see: "PSRAM: APS6404L detected and ready"
   ```

4. **Run memory test:**

   ```c
   // In your main.c or via USB command
   if (psram_test_memory(1024)) {  // Test 1MB
       printf("PSRAM test passed!\n");
   }
   ```

---

## 7. Code Examples

### Complete Helper Functions Module

Here's a complete implementation of the QMI helper functions:

```c
// ============================================================================
// QMI Direct Mode Helper Functions
// ============================================================================

// Enable QMI direct mode with CS1
static inline void qmi_direct_mode_enter(void) {
    qmi_hw->direct_csr = 
        QMI_DIRECT_CSR_EN_BITS |              // Enable direct mode
        QMI_DIRECT_CSR_AUTO_CS1N_BITS |       // Auto-control CS1
        (PSRAM_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB);  // Clock divisor
}

// Disable QMI direct mode
static inline void qmi_direct_mode_exit(void) {
    qmi_hw->direct_csr = 0;
}

// Wait for TX FIFO to be not full
static bool qmi_wait_tx_ready(void) {
    for (int i = 0; i < FIFO_TIMEOUT; i++) {
        if (!(qmi_hw->direct_csr & QMI_DIRECT_CSR_TXFULL_BITS)) {
            return true;
        }
    }
    return false;
}

// Wait for RX FIFO to be not empty
static bool qmi_wait_rx_ready(void) {
    for (int i = 0; i < FIFO_TIMEOUT; i++) {
        if (!(qmi_hw->direct_csr & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            return true;
        }
    }
    return false;
}

// Send a byte via TX FIFO
static bool qmi_tx_byte(uint8_t data) {
    if (!qmi_wait_tx_ready()) {
        return false;
    }
    qmi_hw->direct_tx = data;
    return true;
}

// Receive a byte from RX FIFO
static bool qmi_rx_byte(uint8_t *data) {
    if (!qmi_wait_rx_ready()) {
        return false;
    }
    *data = (uint8_t)(qmi_hw->direct_rx & 0xFF);
    return true;
}
```

### Example: Reading a Block of Data

```c
// Example: Read 256 bytes from PSRAM
uint8_t buffer[256];
if (psram_read(0x100000, buffer, sizeof(buffer))) {
    printf("Read successful!\n");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
} else {
    printf("Read failed!\n");
}
```

### Example: Writing and Verifying Data

```c
// Example: Write test pattern and verify
uint8_t write_data[128];
uint8_t read_data[128];

// Fill with test pattern
for (int i = 0; i < 128; i++) {
    write_data[i] = i & 0xFF;
}

// Write to PSRAM
if (psram_write(0x200000, write_data, 128)) {
    printf("Write successful\n");
    
    // Read back
    if (psram_read(0x200000, read_data, 128)) {
        // Verify
        bool match = true;
        for (int i = 0; i < 128; i++) {
            if (read_data[i] != write_data[i]) {
                printf("Mismatch at offset %d: wrote 0x%02X, read 0x%02X\n",
                       i, write_data[i], read_data[i]);
                match = false;
            }
        }
        if (match) {
            printf("Verification successful!\n");
        }
    }
}
```

### Advanced: Entering Quad Mode (Optional)

For faster transfers, you can enable Quad SPI mode:

```c
static bool psram_enter_quad_mode(void) {
    qmi_direct_mode_enter();
    
    // Send Enter Quad Mode command (0x35)
    if (!qmi_tx_byte(0x35)) {
        qmi_direct_mode_exit();
        return false;
    }
    
    qmi_direct_mode_exit();
    busy_wait_us(10);
    
    printf("PSRAM: Entered Quad SPI mode\n");
    return true;
}

// Call this after psram_init() for better performance
// Then use PSRAM_CMD_FAST_READ_QUAD (0xEB) and PSRAM_CMD_QUAD_WRITE (0x38)
```

---

## 8. Testing Strategy

### Phase 1: Basic Connectivity

**Goal:** Verify GPIO47 configuration and QMI communication

**Tests:**

1. ✅ GPIO47 function set correctly
2. ✅ QMI direct mode activates
3. ✅ PSRAM responds to reset commands
4. ✅ Read ID returns expected values (0x0D, 0x5D)

**Expected Output:**

```
PSRAM: Initializing APS6404L on QMI CS1 (GPIO47)...
PSRAM: Reset complete
PSRAM: ID read - Manufacturer: 0x0D, Device: 0x5D
PSRAM: APS6404L detected and ready
```

**Troubleshooting:**

- If ID read fails: Check GPIO47 function, verify PSRAM power connections
- If ID wrong: Verify PSRAM part number, check for solder bridges
- If timeout: Check QSPI clock divisor, try increasing it

### Phase 2: Single Byte Operations

**Goal:** Verify basic read/write functionality

**Tests:**

```c
// Test 1: Write and read single byte
uint8_t test_val = 0x42;
psram_write(0x000000, &test_val, 1);
uint8_t read_val = 0x00;
psram_read(0x000000, &read_val, 1);
assert(read_val == test_val);

// Test 2: Write different values to different addresses
for (uint8_t i = 0; i < 16; i++) {
    psram_write(i * 256, &i, 1);
}
for (uint8_t i = 0; i < 16; i++) {
    uint8_t val;
    psram_read(i * 256, &val, 1);
    assert(val == i);
}
```

### Phase 3: Block Operations

**Goal:** Test larger transfers and memory boundaries

**Tests:**

```c
// Test 1: 1KB block write/read
uint8_t large_buffer[1024];
for (int i = 0; i < 1024; i++) {
    large_buffer[i] = (i * 17) & 0xFF;
}
psram_write(0x010000, large_buffer, 1024);
memset(large_buffer, 0, 1024);
psram_read(0x010000, large_buffer, 1024);
// Verify all 1024 bytes

// Test 2: Page boundary crossing
uint8_t boundary_test[2048];
psram_write(PSRAM_PAGE_SIZE - 512, boundary_test, 2048);
```

### Phase 4: Stress Testing

**Goal:** Verify reliability under continuous operation

**Tests:**

```c
// Use psram_test_memory() function
bool result = psram_test_memory(8192);  // Test 8MB (full chip)
if (result) {
    printf("Full chip test PASSED\n");
} else {
    printf("Full chip test FAILED\n");
}
```

### Phase 5: Performance Testing

**Goal:** Measure actual throughput

**Tests:**

```c
uint8_t speed_test_buffer[32768];  // 32KB
uint32_t start_time = time_us_32();

// Write test
for (int i = 0; i < 32; i++) {
    psram_write(i * 32768, speed_test_buffer, 32768);
}

uint32_t write_time = time_us_32() - start_time;
float write_speed = (32.0 * 32768.0) / write_time;  // MB/s
printf("Write speed: %.2f MB/s\n", write_speed);

// Read test
start_time = time_us_32();
for (int i = 0; i < 32; i++) {
    psram_read(i * 32768, speed_test_buffer, 32768);
}
uint32_t read_time = time_us_32() - start_time;
float read_speed = (32.0 * 32768.0) / read_time;
printf("Read speed: %.2f MB/s\n", read_speed);
```

**Expected Performance (Direct Mode):**

- Read: 5-10 MB/s
- Write: 5-10 MB/s

**Expected Performance (XIP Mode, if implemented):**

- Read: 20-40 MB/s
- Write: 20-40 MB/s

### Debugging Tips

**Problem:** PSRAM reads return all 0xFF or all 0x00

**Solution:**

- Check power supply to PSRAM (3.3V)
- Verify GPIO47 is configured as QMI function
- Check QSPI data lines for shorts or opens
- Try reducing clock speed (increase PSRAM_CLKDIV)

**Problem:** Intermittent read/write failures

**Solution:**

- Increase FIFO_TIMEOUT value
- Add delay after qmi_direct_mode_enter()
- Check for electrical noise on QSPI lines
- Verify ground connections

**Problem:** ID read works but data operations fail

**Solution:**

- Verify address byte order (big-endian)
- Check dummy byte requirements for Fast Read
- Try using standard Read (0x03) instead of Fast Read (0x0B)
- Ensure chip select timing is adequate

---

## 9. References

### Datasheets and Technical Documents

1. **RP2350 Datasheet**
   - <https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf>
   - Chapter: QMI (QSPI Memory Interface)
   - Section: Direct Mode and Memory Windows

2. **APS6404L-3SQR-SN Datasheet**
   - APMemory 64Mb (8MB) QSPI PSRAM
   - Command set, timing specifications, electrical characteristics

3. **Pico SDK Documentation**
   - hardware/structs/qmi.h - QMI register definitions
   - hardware/gpio.h - GPIO function configuration
   - hardware/clocks.h - System clock management

### Pico SDK Source Files

Located in `$PICO_SDK_PATH/src/`:

- `rp2350/hardware_structs/include/hardware/structs/qmi.h` - QMI hardware structure
- `rp2350/hardware_regs/include/hardware/regs/qmi.h` - QMI register definitions
- `rp2350/boot_stage2/boot2_w25q080.S` - Example of QMI configuration for flash
- `rp2_common/hardware_gpio/` - GPIO function configuration

### Example Projects

1. **RP2350 Boot Stage 2**
   - Shows how to configure QMI for QSPI flash
   - Similar approach works for PSRAM
   - Location: `pico-sdk/src/rp2350/boot_stage2/`

2. **Flash Programming Examples**
   - `hardware_flash` library shows direct mode usage
   - Demonstrates FIFO management and timing

### Community Resources

1. **Raspberry Pi Forums**
   - RP2350 Hardware section
   - Search for "QMI" or "PSRAM" discussions

2. **Pico SDK GitHub**
   - <https://github.com/raspberrypi/pico-sdk>
   - Issues and discussions about QMI usage

### Related Documentation

- **Memory-Map.md** - Your project's memory mapping strategy
- **Hardware-Connection.md** - Physical connections and pin assignments
- **board_config.h** - GPIO pin definitions including GPIO_PSRAM_CS

### Command Reference

Quick reference for APS6404L commands:

| Command | Code | Use Case |
|---------|------|----------|
| Reset Enable | 0x66 | Before reset |
| Reset | 0x99 | Initialize chip |
| Read ID | 0x9F | Verify presence |
| Read | 0x03 | Standard read (SPI) |
| Fast Read | 0x0B | Faster read (SPI, 1 dummy byte) |
| Write | 0x02 | Standard write (SPI) |
| Enter Quad | 0x35 | Enable Quad SPI mode |
| Fast Read Quad | 0xEB | Quad read (6 dummy cycles) |
| Quad Write | 0x38 | Quad write |
| Exit Quad | 0xF5 | Return to SPI mode |

---

## Summary

This plan provides a complete roadmap for implementing PSRAM support on the RP2350 using the QMI peripheral. The key points are:

1. **GPIO47 must be configured as QMI CS1** using `gpio_set_function()`, not as regular GPIO
2. **Use QMI Direct Mode** for flexible, debugging-friendly implementation
3. **Follow the APS6404L initialization sequence:** reset, read ID, verify
4. **Implement proper FIFO management** for reliable communication
5. **Test incrementally** from basic connectivity to full memory tests

By following this plan, you'll have a working PSRAM implementation that:

- ✅ Properly interfaces with the QMI peripheral
- ✅ Supports 8MB of external PSRAM
- ✅ Provides reliable read/write operations
- ✅ Can be optimized later with XIP mode if needed

**Next Steps:**

1. Review this plan and understand the QMI architecture
2. Implement the changes step-by-step in `src/psram.c`
3. Test each phase before moving to the next
4. Report any issues or unexpected behavior

Good luck with your PSRAM implementation!
