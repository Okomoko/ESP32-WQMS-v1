// modbus_tcp.h
// Lightweight MODBUS TCP slave - registers only

#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_map.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize MODBUS TCP slave
void modbus_init(void);

// Start MODBUS server
void modbus_start(void);

// Stop MODBUS server
void modbus_stop(void);

// Update a register value (called by sensors/relays)
void modbus_update_register(uint16_t addr, uint16_t value);

// Read a register value
uint16_t modbus_read_register(uint16_t addr);

// Write a register value (with validation)
int modbus_write_register(uint16_t addr, uint16_t value);

// Get MODBUS status
void modbus_get_status(uint32_t *packet_count, uint32_t *error_count);

#endif // MODBUS_TCP_H