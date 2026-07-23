// web_console.h
#ifndef WEB_CONSOLE_H
#define WEB_CONSOLE_H

#include <stddef.h>
#include "esp_err.h"
#include "http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_console_init(void);
esp_err_t web_console_handler(httpd_req_t *req);
void web_console_write(const char* data);
void web_console_write_len(const char* data, size_t len);
void web_console_print_stats(void);
void web_console_clear(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_CONSOLE_H