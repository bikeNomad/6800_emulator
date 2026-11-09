#pragma once
#include <stdint.h>
void bus_read_block_with_eclock(uint16_t address, uint16_t length, uint8_t *buffer);
void bus_write_block_with_eclock(uint16_t address, const uint8_t *buffer, uint16_t length);
