// api_config.h
// Configuration API endpoints header

#ifndef API_CONFIG_H
#define API_CONFIG_H

#include "esp_http_server.h"

// ============================================================
// Function Prototypes
// ============================================================

// Sensor configuration endpoints
esp_err_t sensors_config_get_handler(httpd_req_t *req);
esp_err_t sensors_config_post_handler(httpd_req_t *req);

// Relay configuration endpoints
esp_err_t relays_config_get_handler(httpd_req_t *req);
esp_err_t relays_config_post_handler(httpd_req_t *req);

#endif // API_CONFIG_H