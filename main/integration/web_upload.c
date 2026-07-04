// web_upload.c
// 3rd party web service integration implementation

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_http_client.h"
#include "cJSON.h"
#include "wifi_manager.h"
#include "web_upload.h"
#include "sensor_read.h"
#include "nvs_config.h"
#include "system_config.h"
#include "watchdog.h"
#include "buffer_recovery.h"
#include "log_levels.h"
#include "logger.h"

// ============================================================
// Static Variables
// ============================================================
static int upload_status = 0;  // 0=idle, 1=uploading, 2=success, 3=failed
static char last_result[128] = {0};

// ============================================================
// Internal Function: HTTP Client
// ============================================================
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            INTEGRATION_LOG_D("HTTP event error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            INTEGRATION_LOG_D("HTTP connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            INTEGRATION_LOG_D("HTTP header sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            INTEGRATION_LOG_D("HTTP header: %.*s", evt->header_len, (char*)evt->header);
            break;
        case HTTP_EVENT_ON_DATA:
            INTEGRATION_LOG_D("HTTP data: %.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_FINISH:
            INTEGRATION_LOG_D("HTTP finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            INTEGRATION_LOG_D("HTTP disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// ============================================================
// Public Functions
// ============================================================

void web_upload_init(void) {
    upload_status = 0;
    snprintf(last_result, sizeof(last_result), "Ready");
    WQMS_LOG_I("Web upload initialized");
}

int web_upload_sensors(void) {
    if (upload_status == 1) {
        INTEGRATION_LOG_W("Upload already in progress");
        return -1;
    }
    
    upload_status = 1;
    
    // 1. Get sensor readings
    sensor_reading_t *readings = sensor_get_all_readings();
    if (!readings) {
        upload_status = 3;
        snprintf(last_result, sizeof(last_result), "No sensor data");
        return -2;
    }
    
    // 2. Build JSON payload
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", wifi_get_mac());
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    
    cJSON *sensors = cJSON_CreateObject();
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (readings[i].status == SENSOR_STATUS_OK) {
            cJSON_AddNumberToObject(sensors, sensor_get_name(i), readings[i].value);
        }
    }
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    char *json_data = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_data) {
        upload_status = 3;
        snprintf(last_result, sizeof(last_result), "JSON creation failed");
        return -3;
    }
    
    // 3. Get URL from NVS
    const char *url = nvs_get_integration_url();
    
    // 4. Send HTTP request
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = INTEGRATION_WEB_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .buffer_size = 2048,
        .keep_alive_enable = false,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));
    
    esp_err_t err = esp_http_client_perform(client);
    
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    // 5. Check result
    if (err == ESP_OK && status_code == 200) {
        upload_status = 2;
        snprintf(last_result, sizeof(last_result), "Upload successful (HTTP %d)", status_code);
        INTEGRATION_LOG_I("Web upload successful");
        free(json_data);                           // ✅ Free after success
        buffer_recovery_clear_pending();
        return 0;
    } else {
        upload_status = 3;
        snprintf(last_result, sizeof(last_result), "Upload failed: HTTP %d", status_code);
        INTEGRATION_LOG_W("Web upload failed: HTTP %d", status_code);
        // Save to buffer for recovery FIRST
        buffer_recovery_add_pending(json_data);    // ✅ Use before free
        free(json_data);                           // ✅ Free after use
        return -4;
    }
}

int web_upload_get_status(void) {
    return upload_status;
}

const char* web_upload_get_last_result(void) {
    return last_result;
}
