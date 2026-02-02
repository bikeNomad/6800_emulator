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
 * TinyUSB Configuration for MC6800 Emulator
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// Board/MCU
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040 // RP2350 uses same USB controller as RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE // No RTOS
#endif

// CFG_TUSB_DEBUG is controlled by compiler -DCFG_TUSB_DEBUG=n
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// Root hub port 0 in device mode
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE)

// Device mode
#define CFG_TUD_ENABLED 1

// EP0 size
#define CFG_TUD_ENDPOINT0_SIZE 64

// Device classes
#define CFG_TUD_CDC    1 // Enable 1 CDC port
#define CFG_TUD_MSC    0 // No mass storage
#define CFG_TUD_HID    0 // No HID
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

// CDC buffer sizes
#define CFG_TUD_CDC_RX_BUFSIZE 512  // Larger for Intel HEX loading
#define CFG_TUD_CDC_TX_BUFSIZE 1024 // Larger to handle multi-line output like HELP
#define CFG_TUD_CDC_EP_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
