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
 * USB CDC Interface
 * Handles communication with host PC for EPROM loading and diagnostics
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include "emulator.h"

// Initialize USB CDC interface
void usb_cdc_init(void);

// Process USB events (call in main loop)
void usb_cdc_task(void);

// Send string to USB
void usb_cdc_send(const char *str);

// Send formatted string to USB
int usb_cdc_printf(const char *restrict fmt, ...);

#endif // USB_CDC_H
