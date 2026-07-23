// log_rotate.h
// Header for log rotation functions

#ifndef LOG_ROTATE_H
#define LOG_ROTATE_H

#include <stdio.h>
#include "esp_err.h"

esp_err_t log_rotate_init(void);
esp_err_t log_rotate_write(const char* data);
size_t log_rotate_read(char* buffer, size_t buffer_size, size_t offset);
size_t log_rotate_get_size(void);
bool log_rotate_is_empty(void);
esp_err_t log_rotate_clear(void);
void log_rotate_flush(void);
void log_rotate_close(void);
bool log_rotate_is_ready(void);

#endif // LOG_ROTATE_H