/**
 * USB CDC Interface
 * Handles communication with host PC for EPROM loading and diagnostics
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>
#include <stdbool.h>

// Initialize USB CDC interface
void usb_cdc_init(void);

// Process USB events (call in main loop)
void usb_cdc_task(void);

// Send string to USB
void usb_cdc_send(const char *str);

// Send formatted string to USB
void usb_cdc_printf(const char *fmt, ...);

#endif // USB_CDC_H
