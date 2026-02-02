/**
 * USB Descriptors for MC6800 Emulator CDC Interface
 */

#include "pico/unique_id.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {.bLength = sizeof(tusb_desc_device_t),
					       .bDescriptorType = TUSB_DESC_DEVICE,
					       .bcdUSB = 0x0200, // USB 2.0
					       .bDeviceClass = TUSB_CLASS_CDC,
					       .bDeviceSubClass = 0,
					       .bDeviceProtocol = 0,
					       .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

					       .idVendor = 0x2E8A,  // Raspberry Pi vendor ID
					       .idProduct = 0x000A, // Raspberry Pi Pico SDK CDC
					       .bcdDevice = 0x0100, // Device version 1.0

					       .iManufacturer = 0x01,
					       .iProduct = 0x02,
					       .iSerialNumber = 0x03,

					       .bNumConfigurations = 0x01};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void)
{
	return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82

static uint8_t const desc_configuration[] = {
	// Config number, interface count, string index, total length, attribute,
	// power in mA
	TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN, 0, 100),

	// Interface number, string index, EP notification address and size, EP data
	// address (out, in) and size
	TUD_CDC_DESCRIPTOR(0, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
	(void)index; // For multiple configurations
	return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// Array of string descriptors
// String 0: Language ID (0x0409 = US English)
// String 1: Manufacturer
// String 2: Product
// String 3: Serial Number (generated from unique board ID)
// String 4: CDC Interface

static char serial_number_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static char const *string_desc_arr[] = {
	(const char[]){0x09, 0x04}, // 0: Language (0x0409 = US English)
	"Raspberry Pi",             // 1: Manufacturer
	"MC6800 Emulator",          // 2: Product
	serial_number_str,          // 3: Serial Number (filled at runtime)
	"MC6800 CDC",               // 4: CDC Interface
};

static uint16_t desc_string[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long
// enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
	(void)langid;

	// Generate serial number on first call
	if (index == 3 && serial_number_str[0] == '\0') {
		pico_unique_board_id_t board_id;
		pico_get_unique_board_id(&board_id);
		for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
			snprintf(&serial_number_str[i * 2], 3, "%02X", board_id.id[i]);
		}
	}

	uint8_t chr_count;

	if (index == 0) {
		// Language ID
		memcpy(&desc_string[1], string_desc_arr[0], 2);
		chr_count = 1;
	} else {
		// Check for valid index
		if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
			return NULL;
		}

		const char *str = string_desc_arr[index];

		// Cap at max char
		chr_count = strlen(str);
		if (chr_count > 31) {
			chr_count = 31;
		}

		// Convert ASCII string into UTF-16
		for (uint8_t i = 0; i < chr_count; i++) {
			desc_string[1 + i] = str[i];
		}
	}

	// First byte is length (including header), second byte is string type
	desc_string[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

	return desc_string;
}
