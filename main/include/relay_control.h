// relay_control.h
// Relay control - 12 outputs with activity duration and cooldown

#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdint.h>
#include "project_defs.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize all relays
void relay_init(void);

// Get relay configuration
relay_config_t* relay_get_config(uint8_t relay_id);

// Get relay state
int relay_get_state(uint8_t relay_id);

// Trigger a relay (ON for configured duration)
int relay_trigger(uint8_t relay_id);

// Trigger a relay with custom duration (overrides default)
int relay_trigger_with_duration(uint8_t relay_id, uint16_t duration_sec);

// Turn a relay ON (manual, no timer)
int relay_on(uint8_t relay_id);

// Turn a relay OFF (manual)
int relay_off(uint8_t relay_id);

// Check if relay is currently active
int relay_is_active(uint8_t relay_id);

// Get remaining time for active relay (seconds)
uint32_t relay_get_remaining(uint8_t relay_id);

// Load relay configuration from NVS
void relay_reload_config(void);

#endif // RELAY_CONTROL_H