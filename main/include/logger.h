// logger.h
// Logging system header

#ifndef LOGGER_H
#define LOGGER_H

#include "log_levels.h"

// Initialize logging system
void log_init(void);

// Write a log entry
void wqms_log_write(wqms_log_type_t type, wqms_log_level_t level, const char *format, ...);

// Flush all logs to disk
void log_flush_all(void);

// Close all log files
void log_close_all(void);

// Backward compatibility
#define log_write wqms_log_write

#endif // LOGGER_H