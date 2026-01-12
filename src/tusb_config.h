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
