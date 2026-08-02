// logger.h - Updated public interface

#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "log_levels.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize logger
void log_init(void);

// Set/get log level
void logger_set_level(uint32_t level);
uint32_t logger_get_level(void);

// Core logging function
void logger_log(uint32_t level, const char* module, const char* format, ...);

// Check if level is enabled
bool logger_is_enabled(uint32_t level);

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H