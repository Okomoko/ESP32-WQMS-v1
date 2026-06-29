// watchdog.h
// Module heartbeat watchdog - detects hung tasks

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>
#include "project_defs.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize the watchdog system
void watchdog_init(void);

// Register a module with its timeout (seconds)
void watchdog_register_module(wdt_module_t module, uint32_t timeout_sec);

// Send heartbeat from a module
void watchdog_heartbeat(wdt_module_t module);

// Get the elapsed time since last heartbeat (for dashboard)
uint32_t watchdog_get_elapsed(wdt_module_t module);

// Get the current timeout for a module
uint32_t watchdog_get_timeout(wdt_module_t module);

// Check if all modules are healthy
int watchdog_all_healthy(void);

#endif // WATCHDOG_H