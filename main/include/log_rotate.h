// log_rotate.h
// Header for log rotation functions

#ifndef LOG_ROTATE_H
#define LOG_ROTATE_H

#include <stdio.h>
#include "log_levels.h"   // For wqms_log_type_t

// Open a log file for the given type (creates or reopens)
FILE* log_rotate_open(wqms_log_type_t type);

// Check if rotation is needed (file size exceeded)
int log_rotate_check(FILE *file, wqms_log_type_t type);

// Close the current log file
void log_rotate_close(wqms_log_type_t type);

// Get the current filename for a log type
const char* log_rotate_get_filename(wqms_log_type_t type, int index);

#endif // LOG_ROTATE_H