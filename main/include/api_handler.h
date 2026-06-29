// api_handler.h
// All API endpoint handlers header

#ifndef API_HANDLER_H
#define API_HANDLER_H

#include "esp_http_server.h"
#include "esp_wifi.h"

// ============================================================
// Function Prototypes
// ============================================================

// Register all API endpoints
void register_api_endpoints(httpd_handle_t server);

// System endpoints
esp_err_t status_get_handler(httpd_req_t *req);
esp_err_t echo_get_handler(httpd_req_t *req);
esp_err_t system_reboot_handler(httpd_req_t *req);

// Sensor endpoints
esp_err_t sensors_get_handler(httpd_req_t *req);

// Relay endpoints
esp_err_t relays_get_handler(httpd_req_t *req);
esp_err_t relay_trigger_post_handler(httpd_req_t *req);
esp_err_t relay_off_post_handler(httpd_req_t *req);

// Configuration endpoints
esp_err_t config_get_handler(httpd_req_t *req);
esp_err_t config_post_handler(httpd_req_t *req);

// WiFi endpoints
esp_err_t wifi_scan_get_handler(httpd_req_t *req);
esp_err_t wifi_connect_post_handler(httpd_req_t *req);
esp_err_t wifi_forget_post_handler(httpd_req_t *req);
esp_err_t wifi_status_get_handler(httpd_req_t *req);

// NTP endpoints
esp_err_t ntp_sync_handler(httpd_req_t *req);
esp_err_t ntp_manual_handler(httpd_req_t *req);

// Calibration endpoints
esp_err_t calibrate_start_handler(httpd_req_t *req);
esp_err_t calibrate_sample_handler(httpd_req_t *req);
esp_err_t calibrate_apply_handler(httpd_req_t *req);
esp_err_t calibrate_cancel_handler(httpd_req_t *req);
esp_err_t calibrate_factor_handler(httpd_req_t *req);

//Log management endpoints
esp_err_t logs_list_handler(httpd_req_t *req);
esp_err_t logs_download_handler(httpd_req_t *req);
esp_err_t logs_delete_handler(httpd_req_t *req);
esp_err_t logs_delete_all_handler(httpd_req_t *req);

// Webhook Notification Endpoints
esp_err_t webhook_config_get_handler(httpd_req_t *req);
esp_err_t webhook_config_post_handler(httpd_req_t *req);
esp_err_t webhook_test_handler(httpd_req_t *req);

// Automation rules endpoints
esp_err_t api_rules_get_handler(httpd_req_t *req);
esp_err_t api_rules_post_handler(httpd_req_t *req);
esp_err_t api_rules_delete_handler(httpd_req_t *req);

#endif // API_HANDLER_H