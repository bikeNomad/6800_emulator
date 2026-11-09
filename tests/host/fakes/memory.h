// Host-test stand-in: flat 64 KiB RAM, $8000-$FFFF reads as "ROM"
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "emulator.h"
typedef enum memory_type_t { MEM_TYPE_UNMAPPED, MEM_TYPE_ROM, MEM_TYPE_RAM } memory_type_t;
extern uint8_t fake_mem[65536];
uint8_t memory_read_fast(uint16_t address);
void memory_write_fast(uint16_t address, uint8_t value);
bool memory_range_has_unmapped(uint16_t address, uint16_t len);
memory_type_t memory_get_type(uint16_t address);
void led_all_off(void);
