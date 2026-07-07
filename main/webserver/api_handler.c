// api_handler.c
// All API endpoint handlers

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "cJSON.h"

#include "api_handler.h"
#include "api_config.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "sensor_read.h"
#include "sensor_calib.h"
#include "sensor_history.h"
#include "relay_control.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "ntp_client.h"
#include "modbus_tcp.h"
#include "watchdog.h"
#include "email_client.h"
#include "rule_manager.h"
#include "automation_engine.h"

#define TAG "API"
// ============================================================
// Helper: Send JSON response
// ============================================================
static void send_json_response(httpd_req_t *req, cJSON *json) {
    char *response = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    free(response);
    cJSON_Delete(json);
}

// ============================================================
// GET /api/status
// ============================================================
esp_err_t status_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "system_name", nvs_get_system_name());
    cJSON_AddStringToObject(root, "version", "v1.0.0");
    cJSON_AddStringToObject(root, "ip", wifi_get_ip());
    cJSON_AddStringToObject(root, "mac", wifi_get_mac());
    cJSON_AddStringToObject(root, "wifi_ssid", wifi_get_ssid());
    cJSON_AddStringToObject(root, "wifi_mode", wifi_get_mode_string());
    
    uint64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    cJSON_AddNumberToObject(root, "uptime_seconds", uptime_sec);
    char uptime_str[32];
    int days = uptime_sec / 86400;
    int hours = (uptime_sec % 86400) / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    if (days > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%dd %dh %dm", days, hours, minutes);
    } else if (hours > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%dh %dm", hours, minutes);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "%dm", minutes);
    }
    cJSON_AddStringToObject(root, "uptime", uptime_str);
    
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "free_heap_kb", esp_get_free_heap_size() / 1024);
    cJSON_AddNumberToObject(root, "cpu_temp_c", 45.0);
    
    cJSON_AddBoolToObject(root, "ntp_synced", ntp_is_synced());
    cJSON_AddStringToObject(root, "timezone", ntp_get_timezone());
    
    uint32_t pkt_count, err_count;
    modbus_get_status(&pkt_count, &err_count);
    cJSON_AddNumberToObject(root, "modbus_packets", pkt_count);
    cJSON_AddNumberToObject(root, "modbus_errors", err_count);
    
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/echo
// ============================================================
esp_err_t echo_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "OK");
    cJSON_AddNumberToObject(root, "code", 200);
    cJSON_AddStringToObject(root, "response", "WQMS");
    
    char *response = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, response, strlen(response));
    free(response);
    cJSON_Delete(root);
    
    return ESP_OK;
}

// ============================================================
// GET /api/sensors
// ============================================================
esp_err_t sensors_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors_array = cJSON_CreateArray();
    
    sensor_reading_t *readings = sensor_get_all_readings();
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddNumberToObject(sensor, "id", i);
        cJSON_AddStringToObject(sensor, "name", sensor_get_name(i));
        cJSON_AddNumberToObject(sensor, "value", readings[i].value);
        cJSON_AddNumberToObject(sensor, "raw_adc", readings[i].raw_adc);
        cJSON_AddNumberToObject(sensor, "status", readings[i].status);
        cJSON_AddNumberToObject(sensor, "quality", readings[i].quality);
        cJSON_AddNumberToObject(sensor, "timestamp", readings[i].timestamp);
        cJSON_AddItemToArray(sensors_array, sensor);
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors_array);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/sensors/config
// ============================================================
esp_err_t sensors_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors_array = cJSON_CreateArray();
    
    // Get sensor configs from NVS
    sensor_config_t configs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(configs, TOTAL_SENSOR_COUNT);
    
    // Get current readings
    sensor_reading_t *readings = sensor_get_all_readings();
    
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddNumberToObject(sensor, "id", i);
        cJSON_AddStringToObject(sensor, "name", configs[i].name);
        cJSON_AddBoolToObject(sensor, "enabled", configs[i].enabled);
        cJSON_AddNumberToObject(sensor, "unit", configs[i].unit);
        cJSON_AddNumberToObject(sensor, "calibration_factor", configs[i].calibration_factor);
        cJSON_AddNumberToObject(sensor, "gpio_pin", configs[i].gpio_pin);
        cJSON_AddNumberToObject(sensor, "adc_channel", configs[i].adc_channel);
        cJSON_AddNumberToObject(sensor, "modbus_register", configs[i].modbus_register);
        cJSON_AddNumberToObject(sensor, "min_value", configs[i].min_value);
        cJSON_AddNumberToObject(sensor, "max_value", configs[i].max_value);
        
        // Add current reading if available
        if (readings && i < TOTAL_SENSOR_COUNT) {
            cJSON_AddNumberToObject(sensor, "current_value", readings[i].value);
            cJSON_AddNumberToObject(sensor, "status", readings[i].status);
        }
        
        cJSON_AddItemToArray(sensors_array, sensor);
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors_array);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/sensors/config
// ============================================================
esp_err_t sensors_config_post_handler(httpd_req_t *req) {
    char buffer[1024];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *sensors = cJSON_GetObjectItem(json, "sensors");
    if (!sensors || !cJSON_IsArray(sensors)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'sensors' array");
        return ESP_FAIL;
    }
    
    // Load current sensor configs from NVS
    sensor_config_t configs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(configs, TOTAL_SENSOR_COUNT);
    
    cJSON *item;
    cJSON_ArrayForEach(item, sensors) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *cal = cJSON_GetObjectItem(item, "calibration_factor");
        cJSON *unit = cJSON_GetObjectItem(item, "unit");
        if (id && cJSON_IsNumber(id)) {
            int idx = id->valueint;
            if (idx >= 0 && idx < TOTAL_SENSOR_COUNT) {
                if (name && cJSON_IsString(name)) {
                    strncpy(configs[idx].name, name->valuestring, sizeof(configs[idx].name) - 1);
                    configs[idx].name[sizeof(configs[idx].name) - 1] = '\0';
                }
                if (enabled && cJSON_IsBool(enabled)) {
                    configs[idx].enabled = enabled->valueint;
                    sensor_set_enabled(idx, enabled->valueint);
                }
                if (cal && cJSON_IsNumber(cal)) {
                    configs[idx].calibration_factor = cal->valueint;
                }
                if (unit && cJSON_IsNumber(unit)) {
                    APP_LOG_I("Sensor: %d, Unit: %d", id->valueint, unit->valueint);
                    configs[idx].unit = unit->valueint;
                }
            }
        }
    }
    
    nvs_save_sensor_config(configs, TOTAL_SENSOR_COUNT);
    cJSON_Delete(json);
    sensor_reload_config();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Sensor configuration saved");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/relays
// ============================================================
esp_err_t relays_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *relays_array = cJSON_CreateArray();
    
    for (int i = 0; i < RELAY_COUNT; i++) {
        relay_config_t *cfg = relay_get_config(i);
        cJSON *relay = cJSON_CreateObject();
        cJSON_AddNumberToObject(relay, "id", i);
        cJSON_AddStringToObject(relay, "name", cfg ? cfg->name : "Unknown");
        cJSON_AddNumberToObject(relay, "state", relay_get_state(i));
        cJSON_AddBoolToObject(relay, "active", relay_is_active(i));
        cJSON_AddNumberToObject(relay, "remaining", relay_get_remaining(i));
        cJSON_AddBoolToObject(relay, "enabled", cfg ? cfg->enabled : 0);
        cJSON_AddNumberToObject(relay, "duration", cfg ? cfg->activity_duration : 0);
        cJSON_AddNumberToObject(relay, "off_delay", cfg ? cfg->off_delay : 0);
        cJSON_AddNumberToObject(relay, "gpio_pin", cfg ? cfg->gpio_pin : 0);
        cJSON_AddItemToArray(relays_array, relay);
    }
    
    cJSON_AddItemToObject(root, "relays", relays_array);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/relays/trigger?relay=2&duration=5
// ============================================================
esp_err_t relay_trigger_post_handler(httpd_req_t *req) {
    char query[64] = {0};
    int relay_id = -1;
    int duration = 0;
    
    // Parse query parameters for relay ID
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param_relay[8] = {0};
        if (httpd_query_key_value(query, "relay", param_relay, sizeof(param_relay)) == ESP_OK) {
            relay_id = atoi(param_relay);
        }
        
        // Optional: get duration from query as well
        char param_duration[16] = {0};
        if (httpd_query_key_value(query, "duration", param_duration, sizeof(param_duration)) == ESP_OK) {
            duration = atoi(param_duration);
        }
    }
/*
    // If relay_id not in query, try to get from JSON body (for backward compatibility)
    if (relay_id < 0 || relay_id >= RELAY_COUNT) {
        char buffer[64];
        int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *relay = cJSON_GetObjectItem(json, "relay");
                if (relay && cJSON_IsNumber(relay)) {
                    relay_id = relay->valueint;
                }
                cJSON *dur = cJSON_GetObjectItem(json, "duration");
                if (dur && cJSON_IsNumber(dur) && duration == 0) {
                    duration = dur->valueint;
                }
                cJSON_Delete(json);
            }
        }
    }
*/
    if (relay_id < 0 || relay_id >= RELAY_COUNT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay ID");
        return ESP_FAIL;
    }
    
    int result;
    if (duration > 0) {
        result = relay_trigger_with_duration(relay_id, duration);
    } else {
        result = relay_trigger(relay_id);
    }

    if (result == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "Relay triggered");
        send_json_response(req, root);
    } else {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Relay trigger failed");
        send_json_response(req, root);
    }
    
    return ESP_OK;
}

// ============================================================
// POST /api/relays/off?relay=2
// ============================================================
esp_err_t relay_off_post_handler(httpd_req_t *req) {
    char query[64] = {0};
    int relay_id = -1;
    
    // Parse query parameter for relay ID
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param_relay[8] = {0};
        if (httpd_query_key_value(query, "relay", param_relay, sizeof(param_relay)) == ESP_OK) {
            relay_id = atoi(param_relay);
        }
    }
    
    // If relay_id not in query, try to get from JSON body (for backward compatibility)
    if (relay_id < 0 || relay_id >= RELAY_COUNT) {
        char buffer[64];
        int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *relay = cJSON_GetObjectItem(json, "relay");
                if (relay && cJSON_IsNumber(relay)) {
                    relay_id = relay->valueint;
                }
                cJSON_Delete(json);
            }
        }
    }
    
    if (relay_id < 0 || relay_id >= RELAY_COUNT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay ID");
        return ESP_FAIL;
    }
    
    relay_off(relay_id);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Relay turned off");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/relays/config
// ============================================================
esp_err_t relays_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *relays_array = cJSON_CreateArray();
    
    for (int i = 0; i < RELAY_COUNT; i++) {
        relay_config_t *cfg = relay_get_config(i);
        cJSON *relay = cJSON_CreateObject();
        cJSON_AddNumberToObject(relay, "id", i);
        cJSON_AddStringToObject(relay, "name", cfg ? cfg->name : "Unknown");
        cJSON_AddBoolToObject(relay, "enabled", cfg ? cfg->enabled : 0);
        cJSON_AddNumberToObject(relay, "gpio_pin", cfg ? cfg->gpio_pin : 0);
        cJSON_AddNumberToObject(relay, "modbus_register", cfg ? cfg->modbus_register : 0);
        cJSON_AddNumberToObject(relay, "duration_ms", cfg ? cfg->activity_duration : 0);
        cJSON_AddNumberToObject(relay, "off_delay_ms", cfg ? cfg->off_delay : 0);
        cJSON_AddItemToArray(relays_array, relay);
    }
    
    cJSON_AddItemToObject(root, "relays", relays_array);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/relays/config
// ============================================================
esp_err_t relays_config_post_handler(httpd_req_t *req) {
    char buffer[2048];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *relays = cJSON_GetObjectItem(json, "relays");
    if (!relays || !cJSON_IsArray(relays)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'relays' array");
        return ESP_FAIL;
    }
    
    relay_config_t configs[RELAY_COUNT];
    nvs_load_relay_config(configs, RELAY_COUNT);
    
    cJSON *item;
    cJSON_ArrayForEach(item, relays) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *duration = cJSON_GetObjectItem(item, "duration_ms");
        cJSON *off_delay = cJSON_GetObjectItem(item, "off_delay_ms");
        
        if (id && cJSON_IsNumber(id)) {
            int idx = id->valueint;
            if (idx >= 0 && idx < RELAY_COUNT) {
                if (name && cJSON_IsString(name)) {
                    strncpy(configs[idx].name, name->valuestring, sizeof(configs[idx].name) - 1);
                    configs[idx].name[sizeof(configs[idx].name) - 1] = '\0';
                }
                if (enabled && cJSON_IsBool(enabled)) {
                    configs[idx].enabled = enabled->valueint;
                }
                if (duration && cJSON_IsNumber(duration)) {
                    configs[idx].activity_duration = duration->valueint;
                }
                if (off_delay && cJSON_IsNumber(off_delay)) {
                    configs[idx].off_delay = off_delay->valueint;
                }
            }
        }
    }
    
    nvs_save_relay_config(configs, RELAY_COUNT);
    cJSON_Delete(json);
    relay_reload_config();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Relay configuration saved");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/config
// ============================================================
esp_err_t config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "system_name", nvs_get_system_name());
    cJSON_AddStringToObject(root, "system_location", nvs_get_system_location());
    cJSON_AddNumberToObject(root, "sample_interval_ms", nvs_get_sample_interval());
    cJSON_AddNumberToObject(root, "modbus_interval_ms", nvs_get_modbus_interval());
    cJSON_AddStringToObject(root, "integration_url", nvs_get_integration_url());
    cJSON_AddNumberToObject(root, "integration_interval_sec", nvs_get_integration_interval());
    cJSON_AddNumberToObject(root, "automation_interval_sec", nvs_get_automation_interval());
    cJSON_AddStringToObject(root, "ntp_servers", ntp_get_servers());
    
    cJSON_AddStringToObject(root, "wifi_ssid", wifi_get_ssid());
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_is_connected());
    
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/config
// ============================================================
esp_err_t config_post_handler(httpd_req_t *req) {
    char buffer[512];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(json, "system_name");
    if (item && cJSON_IsString(item)) {
        nvs_set_system_name(item->valuestring);
        API_LOG_I("System name updated to: %s", item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "system_location");
    if (item && cJSON_IsString(item)) {
        nvs_set_system_location(item->valuestring);
        API_LOG_I("System location updated to: %s", item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "sample_interval_ms");
    if (item && cJSON_IsNumber(item)) {
        nvs_set_sample_interval(item->valueint);
        API_LOG_I("Sample interval updated to: %d ms", item->valueint);
    }
    
    item = cJSON_GetObjectItem(json, "modbus_interval_ms");
    if (item && cJSON_IsNumber(item)) {
        nvs_set_modbus_interval(item->valueint);
        API_LOG_I("MODBUS interval updated to: %d ms", item->valueint);
    }
    
    item = cJSON_GetObjectItem(json, "integration_url");
    if (item && cJSON_IsString(item)) {
        nvs_set_integration_url(item->valuestring);
        API_LOG_I("Integration URL updated to: %s", item->valuestring);
    }
    
    item = cJSON_GetObjectItem(json, "integration_interval_sec");
    if (item && cJSON_IsNumber(item)) {
        nvs_set_integration_interval(item->valueint);
        API_LOG_I("Integration interval updated to: %d sec", item->valueint);
    }
    
    item = cJSON_GetObjectItem(json, "automation_interval_sec");
    if (item && cJSON_IsNumber(item)) {
        nvs_set_automation_interval(item->valueint);
        API_LOG_I("Automation interval updated to: %d sec", item->valueint);
    }
    
    item = cJSON_GetObjectItem(json, "ntp_servers");
    if (item && cJSON_IsString(item)) {
        API_LOG_I("Updating NTP servers: %s", item->valuestring);
        ntp_set_servers(item->valuestring);
    }
    
    cJSON_Delete(json);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Configuration saved");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/wifi/scan
// ============================================================
esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    API_LOG_I("WiFi scan request received");
    
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    wifi_mode_t current_mode;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK) {
        API_LOG_E("Failed to get WiFi mode: %s", esp_err_to_name(err));
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddStringToObject(root, "error", "WiFi not initialized");
        send_json_response(req, root);
        return ESP_OK;
    }
    
    if (current_mode == WIFI_MODE_AP || current_mode == WIFI_MODE_NULL) {
        API_LOG_I("WiFi in AP mode, enabling STA for scan");
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            API_LOG_E("Failed to set STA mode for scan: %s", esp_err_to_name(err));
            cJSON_AddItemToObject(root, "networks", networks);
            cJSON_AddStringToObject(root, "error", "Failed to set STA mode");
            send_json_response(req, root);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };
    
    err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        API_LOG_E("WiFi scan start failed: %s", esp_err_to_name(err));
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddStringToObject(root, "error", "Scan start failed");
        send_json_response(req, root);
        if (current_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        API_LOG_E("Failed to get AP count: %s", esp_err_to_name(err));
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddStringToObject(root, "error", "Failed to get scan results");
        send_json_response(req, root);
        if (current_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    API_LOG_I("WiFi scan found %d networks", ap_count);
    
    if (ap_count == 0) {
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddNumberToObject(root, "total", 0);
        send_json_response(req, root);
        if (current_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    if (ap_count > 20) ap_count = 20;
    
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        API_LOG_E("Failed to allocate memory for AP records");
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddStringToObject(root, "error", "Out of memory");
        send_json_response(req, root);
        if (current_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err != ESP_OK) {
        API_LOG_E("Failed to get AP records: %s", esp_err_to_name(err));
        free(ap_records);
        cJSON_AddItemToObject(root, "networks", networks);
        cJSON_AddStringToObject(root, "error", "Failed to get AP records");
        send_json_response(req, root);
        if (current_mode == WIFI_MODE_AP) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
        return ESP_OK;
    }
    
    for (int i = 0; i < ap_count; i++) {
        cJSON *net = cJSON_CreateObject();
        
        char ssid[33];
        memcpy(ssid, ap_records[i].ssid, 32);
        ssid[32] = '\0';
        cJSON_AddStringToObject(net, "ssid", ssid);
        
        cJSON_AddNumberToObject(net, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(net, "channel", ap_records[i].primary);
        cJSON_AddBoolToObject(net, "secured", ap_records[i].authmode != WIFI_AUTH_OPEN);
        
        const char *auth_str = "Unknown";
        switch (ap_records[i].authmode) {
            case WIFI_AUTH_OPEN: auth_str = "Open"; break;
            case WIFI_AUTH_WEP: auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth_str = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth_str = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth_str = "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "WPA2/WPA3"; break;
            default: auth_str = "Unknown"; break;
        }
        cJSON_AddStringToObject(net, "authmode", auth_str);
        
        cJSON_AddItemToArray(networks, net);
    }
    
    free(ap_records);
    
    cJSON_AddItemToObject(root, "networks", networks);
    cJSON_AddNumberToObject(root, "total", ap_count);
    send_json_response(req, root);
    
    API_LOG_I("WiFi scan completed: %d networks returned", ap_count);
    
    if (current_mode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_AP);
        wifi_config_t ap_config;
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK) {
            esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        }
        esp_wifi_start();
        API_LOG_I("Restored AP mode after scan");
    }
    
    return ESP_OK;
}

// ============================================================
// POST /api/wifi/connect
// ============================================================
esp_err_t wifi_connect_post_handler(httpd_req_t *req) {
    char buffer[256];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *password = cJSON_GetObjectItem(json, "password");
    cJSON *mode = cJSON_GetObjectItem(json, "mode");
    
    cJSON *root = cJSON_CreateObject();
    
    if (mode && cJSON_IsNumber(mode)) {
        int new_mode = mode->valueint;
        int result = wifi_set_mode(new_mode);
        if (result == 0 && new_mode == 0) {
            cJSON_AddBoolToObject(root, "success", true);
            cJSON_AddStringToObject(root, "message", "Switched to AP mode. Reconnect to new network.");
            cJSON_AddStringToObject(root, "ssid", wifi_get_ap_ssid());
            cJSON_AddStringToObject(root, "password", wifi_get_ap_password());
            cJSON_AddStringToObject(root, "ip", "192.168.4.1");
            send_json_response(req, root);
            cJSON_Delete(json);
            return ESP_OK;
        } else if (result != 0) {
            cJSON_AddBoolToObject(root, "success", false);
            cJSON_AddStringToObject(root, "message", "Failed to set WiFi mode");
            send_json_response(req, root);
            cJSON_Delete(json);
            return ESP_OK;
        }
    }
    
    if (ssid && cJSON_IsString(ssid) && password && cJSON_IsString(password)) {
        int result = wifi_connect_sta(ssid->valuestring, password->valuestring);
        if (result == 0) {
            cJSON_AddBoolToObject(root, "success", true);
            cJSON_AddStringToObject(root, "message", "Connected to network! Reconnect to new IP.");
            cJSON_AddStringToObject(root, "ssid", ssid->valuestring);
            cJSON_AddStringToObject(root, "ip", wifi_get_ip());
        } else {
            cJSON_AddBoolToObject(root, "success", false);
            cJSON_AddStringToObject(root, "message", "Connection failed. Check password and try again.");
        }
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Missing SSID or password");
    }
    
    cJSON_Delete(json);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/wifi/forget
// ============================================================
esp_err_t wifi_forget_post_handler(httpd_req_t *req) {
    wifi_forget_network();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Network credentials are cleared, system will restart");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/wifi/status
// ============================================================
esp_err_t wifi_status_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", wifi_get_mode_string());
    cJSON_AddNumberToObject(root, "mode_id", wifi_get_mode());
    cJSON_AddStringToObject(root, "ssid", wifi_get_ssid());
    cJSON_AddStringToObject(root, "ap_ssid", wifi_get_ap_ssid());
    cJSON_AddStringToObject(root, "ap_password", wifi_get_ap_password());
    cJSON_AddStringToObject(root, "ip", wifi_get_ip());
    cJSON_AddStringToObject(root, "mac", wifi_get_mac());
    cJSON_AddBoolToObject(root, "connected", wifi_is_connected());
    cJSON_AddNumberToObject(root, "rssi", wifi_get_rssi());
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/calibrate/start
// ============================================================
esp_err_t calibrate_start_handler(httpd_req_t *req) {
    char buffer[128];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *sensor_id = cJSON_GetObjectItem(json, "sensor_id");
    cJSON *root = cJSON_CreateObject();
    
    if (!sensor_id || !cJSON_IsNumber(sensor_id)) {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Missing sensor_id");
        cJSON_Delete(json);
        send_json_response(req, root);
        return ESP_OK;
    }
    
    int id = sensor_id->valueint;
    int result = cal_start(id);
    
    if (result == 0) {
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "Calibration started");
        cJSON_AddNumberToObject(root, "sensor_id", id);
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Failed to start calibration");
    }
    
    cJSON_Delete(json);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/calibrate/sample
// ============================================================
esp_err_t calibrate_sample_handler(httpd_req_t *req) {
    char buffer[128];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *known_value = cJSON_GetObjectItem(json, "known_value");
    cJSON *root = cJSON_CreateObject();
    
    if (!known_value || !cJSON_IsNumber(known_value)) {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Missing known_value");
        cJSON_Delete(json);
        send_json_response(req, root);
        return ESP_OK;
    }
    
    // Check if calibration is active
    if (!cal_is_active()) {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "No active calibration session");
        cJSON_Delete(json);
        send_json_response(req, root);
        return ESP_OK;
    }
    
    float value = known_value->valuedouble;
    int result = cal_add_sample(value);
    
    if (result == 0) {
        cal_session_t *session = cal_get_session();
        int sample_count = cal_get_sample_count();
        float factor = cal_calculate_factor();
        
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddNumberToObject(root, "sample_number", sample_count);
        cJSON_AddNumberToObject(root, "known_value", value);
        
        if (session && sample_count > 0) {
            cJSON_AddNumberToObject(root, "raw_adc", session->samples[sample_count - 1].raw_adc);
            cJSON_AddNumberToObject(root, "voltage", session->samples[sample_count - 1].voltage);
        }
        
        if (factor > 0) {
            cJSON_AddNumberToObject(root, "factor", factor);
        }
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Failed to add sample");
    }
    
    cJSON_Delete(json);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/calibrate/apply
// ============================================================
esp_err_t calibrate_apply_handler(httpd_req_t *req) {
    // No need to parse body for apply - it's just an action
    // But we still need to handle the case where body might be empty
    
    char buffer[128];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    
    // If there's a body, parse it (ignore if empty)
    cJSON *json = NULL;
    if (len > 0) {
        buffer[len] = '\0';
        json = cJSON_Parse(buffer);
        // If parsing fails, it's not a critical error for apply
        if (!json) {
            API_LOG_W("Failed to parse JSON body, continuing anyway");
        }
    }
    
    cJSON *root = cJSON_CreateObject();
    
    // Check if calibration is active
    if (!cal_is_active()) {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "No active calibration session");
        send_json_response(req, root);
        if (json) cJSON_Delete(json);
        return ESP_OK;
    }
    
    // Check if we have enough samples
    int sample_count = cal_get_sample_count();
    if (sample_count < 2) {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Need at least 2 samples");
        send_json_response(req, root);
        if (json) cJSON_Delete(json);
        return ESP_OK;
    }
    
    // Apply calibration
    int result = cal_apply();
    if (result == 0) {
        float factor = cal_calculate_factor();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddNumberToObject(root, "factor", factor);
        API_LOG_I("Coef: %f", factor);
        cJSON_AddStringToObject(root, "message", "Calibration applied successfully");
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Failed to apply calibration");
    }
    
    send_json_response(req, root);
    if (json) cJSON_Delete(json);
    return ESP_OK;
}

// ============================================================
// POST /api/calibrate/cancel
// ============================================================
esp_err_t calibrate_cancel_handler(httpd_req_t *req) {
    // No need to parse body for cancel
    // Read body if present but ignore it
    char buffer[64];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len > 0) {
        // Body present but we ignore it
        buffer[len] = '\0';
    }
    
    cJSON *root = cJSON_CreateObject();
    
    int result = cal_cancel();
    if (result == 0) {
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "Calibration cancelled");
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "No active calibration to cancel");
    }
    
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/calibrate/factor
// ============================================================
esp_err_t calibrate_factor_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    float factor = cal_calculate_factor();
    int sample_count = cal_get_sample_count();
    
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "factor", factor);
    cJSON_AddNumberToObject(root, "sample_count", sample_count);
    
    if (sample_count < 2) {
        cJSON_AddStringToObject(root, "message", "Need at least 2 samples");
    } else {
        cJSON_AddStringToObject(root, "message", "Factor calculated");
    }
    
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/ntp/sync
// ============================================================
esp_err_t ntp_sync_handler(httpd_req_t *req) {
    int result = ntp_force_sync();
    cJSON *root = cJSON_CreateObject();
    if (result == 0) {
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "NTP sync initiated");
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Failed to start NTP sync");
    }
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/ntp/manual
// ============================================================
esp_err_t ntp_manual_handler(httpd_req_t *req) {
    char buffer[128];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");
    cJSON *root = cJSON_CreateObject();
    
    if (timestamp && cJSON_IsNumber(timestamp)) {
        ntp_set_manual_time((uint32_t)timestamp->valueint);
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "Manual time set");
    } else {
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "message", "Missing timestamp");
    }
    
    cJSON_Delete(json);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/system/reboot
// ============================================================
esp_err_t system_reboot_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "System rebooting...");
    
    char *response = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    free(response);
    cJSON_Delete(root);
    
    log_flush_all();
    esp_restart();
    
    return ESP_OK;
}

// ============================================================
// GET /api/modbus/map
// ============================================================
esp_err_t modbus_map_get_handler(httpd_req_t *req) {
    modbus_map_entry_t map[MODBUS_MAP_ENTRY_COUNT];
    nvs_load_modbus_map(map, MODBUS_MAP_ENTRY_COUNT);
    
    cJSON *root = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    
    for (int i = 0; i < MODBUS_MAP_ENTRY_COUNT; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "index", i);
        cJSON_AddNumberToObject(entry, "address", map[i].address);
        cJSON_AddStringToObject(entry, "type", map[i].type);
        cJSON_AddStringToObject(entry, "description", map[i].description);
        cJSON_AddStringToObject(entry, "access", map[i].access);
        cJSON_AddItemToArray(entries, entry);
    }
    
    cJSON_AddItemToObject(root, "entries", entries);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// POST /api/modbus/map
// ============================================================
esp_err_t modbus_map_post_handler(httpd_req_t *req) {
    char buffer[2048];
    int len = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buffer[len] = '\0';
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *entries = cJSON_GetObjectItem(json, "entries");
    if (!entries || !cJSON_IsArray(entries)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'entries' array");
        return ESP_FAIL;
    }
    
    modbus_map_entry_t map[MODBUS_MAP_ENTRY_COUNT];
    nvs_load_modbus_map(map, MODBUS_MAP_ENTRY_COUNT);
    
    cJSON *item;
    cJSON_ArrayForEach(item, entries) {
        cJSON *idx = cJSON_GetObjectItem(item, "index");
        cJSON *addr = cJSON_GetObjectItem(item, "address");
        
        if (idx && cJSON_IsNumber(idx) && addr && cJSON_IsNumber(addr)) {
            int index = idx->valueint;
            if (index >= 0 && index < MODBUS_MAP_ENTRY_COUNT) {
                map[index].address = addr->valueint;
            }
        }
    }
    
    nvs_save_modbus_map(map, MODBUS_MAP_ENTRY_COUNT);
    cJSON_Delete(json);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "MODBUS map saved");
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// GET /api/logs - List all logs OR download specific log
// ============================================================
esp_err_t logs_get_handler(httpd_req_t *req) {
    char query[128] = {0};
    char name[64] = {0};
    int has_name = 0;
    
    // Check if 'name' query parameter exists
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK) {
            has_name = 1;
        }
    }
    
    // Case 1: Download specific log file
    if (has_name && strlen(name) > 0) {
        API_LOG_I("Downloading log: %s", name);
        
        char path[128];
        snprintf(path, sizeof(path), "/spiffs/logs/%.100s", name);
        
        FILE *f = fopen(path, "r");
        if (!f) {
            API_LOG_E("File not found: %s", path);
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }
        
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char *buffer = malloc(size);
        if (!buffer) {
            fclose(f);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        fread(buffer, 1, size, f);
        fclose(f);
        
        httpd_resp_set_type(req, "text/plain");
        char header[128];
        snprintf(header, sizeof(header), "attachment; filename=\"%s\"", name);
        httpd_resp_set_hdr(req, "Content-Disposition", header);
        httpd_resp_send(req, buffer, size);
        free(buffer);
        
        API_LOG_I("Log downloaded: %s (%ld bytes)", name, size);
        return ESP_OK;
    }
    
    // Case 2: List all log files
    cJSON *root = cJSON_CreateObject();
    cJSON *logs_array = cJSON_CreateArray();
    
    DIR *dir = opendir("/spiffs/logs");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                char path[128];
                snprintf(path, sizeof(path), "/spiffs/logs/%.100s", entry->d_name);
                struct stat st;
                if (stat(path, &st) == 0) {
                    cJSON *log = cJSON_CreateObject();
                    cJSON_AddStringToObject(log, "name", entry->d_name);
                    cJSON_AddNumberToObject(log, "size", st.st_size);
                    cJSON_AddNumberToObject(log, "modified", st.st_mtime);
                    cJSON_AddItemToArray(logs_array, log);
                }
            }
        }
        closedir(dir);
    } else {
        API_LOG_W("Logs directory not found: /spiffs/logs");
    }
    
    cJSON_AddItemToObject(root, "logs", logs_array);
    send_json_response(req, root);
    return ESP_OK;
}

// ============================================================
// DELETE /api/logs - Delete specific log OR all logs
// ============================================================
esp_err_t logs_delete_handler(httpd_req_t *req) {
    // Check if it's the "all" endpoint
    if (strstr(req->uri, "/api/logs/all") != NULL) {
        // Delete all logs
        int deleted = 0;
        DIR *dir = opendir("/spiffs/logs");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {
                    char path[128];
                    snprintf(path, sizeof(path), "/spiffs/logs/%.100s", entry->d_name);
                    if (unlink(path) == 0) deleted++;
                }
            }
            closedir(dir);
        }
        
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddNumberToObject(root, "deleted", deleted);
        cJSON_AddStringToObject(root, "message", "Logs deleted");
        send_json_response(req, root);
        return ESP_OK;
    }
    
    // Delete specific log
    char query[128] = {0};
    char name[64] = {0};
    
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "name", name, sizeof(name));
    }
    
    if (strlen(name) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename");
        return ESP_FAIL;
    }
    
    char path[128];
    snprintf(path, sizeof(path), "/spiffs/logs/%s", name);
    
    if (unlink(path) == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", true);
        cJSON_AddStringToObject(root, "message", "Log deleted");
        send_json_response(req, root);
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }
}

// ============================================================
// Automation Rule API Endpoints
// ============================================================
// ============================================================
// GET /api/rules - Get all automation rules (WITH CONDITIONS & OUTPUTS)
// ============================================================
esp_err_t api_rules_get_handler(httpd_req_t *req) {
    // Step 1: Get rules count safely
    int count = rule_get_count();
    
    // If no rules, return empty array immediately
    if (count <= 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"rules\":[],\"count\":0}", -1);
        return ESP_OK;
    }
    
    // Step 2: Get rules pointer
    automation_rule_t *rules = rule_get_all();
    
    // Safety check: if rules is NULL, return empty
    if (rules == NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"rules\":[],\"count\":0}", -1);
        return ESP_OK;
    }
    
    // Step 3: Build response with FULL data
    char response[8192] = "{ \"rules\": [";
    int pos = strlen(response);
    int found = 0;
    
    int max_rules = MAX_RULES;
    if (max_rules > 32) max_rules = 32;
    
    for (int i = 0; i < max_rules && pos < (int)sizeof(response) - 500; i++) {
        // Check if this rule slot is actually used
        int is_valid = 0;
        if (rules[i].name[0] != '\0' && rules[i].name[0] != 0xFF) {
            int name_len = 0;
            int valid_chars = 1;
            for (int j = 0; j < 64 && rules[i].name[j] != '\0'; j++) {
                name_len++;
                if (rules[i].name[j] < 32 || rules[i].name[j] > 126) {
                    valid_chars = 0;
                    break;
                }
            }
            if (name_len > 0 && name_len < 64 && valid_chars) {
                is_valid = 1;
            }
        }
        
        if (!is_valid) {
            continue;
        }
        
        if (found > 0) {
            pos += snprintf(response + pos, sizeof(response) - pos, ",");
        }
        found++;
        
        // Safe strings
        char safe_name[64] = "Unnamed";
        strncpy(safe_name, rules[i].name, 63);
        safe_name[63] = '\0';
        
        char safe_recipient[64] = "";
        if (rules[i].email_recipient[0] != '\0' && rules[i].email_recipient[0] != 0xFF) {
            strncpy(safe_recipient, rules[i].email_recipient, 63);
            safe_recipient[63] = '\0';
        }
        
        char safe_subject[128] = "";
        if (rules[i].email_subject[0] != '\0' && rules[i].email_subject[0] != 0xFF) {
            strncpy(safe_subject, rules[i].email_subject, 127);
            safe_subject[127] = '\0';
        }
        
        // ============================================================
        // FIX: Build conditions JSON array
        // ============================================================
        char conditions_str[512] = "";
        int cond_pos = 0;
        int cond_count = (rules[i].condition_count <= 4) ? rules[i].condition_count : 4;
        for (int j = 0; j < cond_count; j++) {
            if (j > 0) {
                cond_pos += snprintf(conditions_str + cond_pos, sizeof(conditions_str) - cond_pos, ",");
            }
            cond_pos += snprintf(conditions_str + cond_pos, sizeof(conditions_str) - cond_pos,
                "{\"sensor_id\":%d,\"comparator\":%d,\"threshold\":%.2f}",
                rules[i].conditions[j].sensor_id,
                rules[i].conditions[j].comparator,
                rules[i].conditions[j].threshold
            );
        }
        
        // ============================================================
        // FIX: Build outputs JSON array
        // ============================================================
        char outputs_str[512] = "";
        int out_pos = 0;
        int out_count = (rules[i].output_count <= 4) ? rules[i].output_count : 4;
        for (int j = 0; j < out_count; j++) {
            if (j > 0) {
                out_pos += snprintf(outputs_str + out_pos, sizeof(outputs_str) - out_pos, ",");
            }
            out_pos += snprintf(outputs_str + out_pos, sizeof(outputs_str) - out_pos,
                "{\"type\":%d,\"id\":%d,\"duration\":%d}",
                rules[i].outputs[j].type,
                rules[i].outputs[j].id,
                rules[i].outputs[j].custom_duration
            );
        }
        
        // ============================================================
        // Build the full rule JSON with conditions and outputs
        // ============================================================
        pos += snprintf(response + pos, sizeof(response) - pos,
            "{"
            "\"id\":%d,"
            "\"name\":\"%s\","
            "\"enabled\":%d,"
            "\"logic_type\":%d,"
            "\"condition_count\":%d,"
            "\"conditions\":[%s],"
            "\"output_count\":%d,"
            "\"outputs\":[%s],"
            "\"cooldown_enabled\":%d,"
            "\"cooldown_seconds\":%d,"
            "\"trigger_count\":%lu,"
            "\"last_triggered\":%lu,"
            "\"created_timestamp\":%ld,"
            "\"last_modified_timestamp\":%ld,"
            "\"email_recipient\":\"%s\","
            "\"email_subject\":\"%s\""
            "}",
            i,
            safe_name,
            rules[i].enabled ? 1 : 0,
            rules[i].logic_type,
            cond_count,
            conditions_str,
            out_count,
            outputs_str,
            (rules[i].cooldown_seconds > 0) ? 1 : 0,
            rules[i].cooldown_seconds,
            (unsigned long)rules[i].trigger_count,
            (unsigned long)rules[i].last_triggered,
            (long)rules[i].created_timestamp,
            (long)rules[i].last_modified_timestamp,
            safe_recipient,
            safe_subject
        );
    }
    
    // Close JSON
    snprintf(response + pos, sizeof(response) - pos, "], \"count\": %d }", found);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// POST /api/rules - Create or update a rule
esp_err_t api_rules_post_handler(httpd_req_t *req) {
    char buffer[4096];  // Increased buffer size
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    API_LOG_I("POST /api/rules - Received: %s", buffer);
    
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Parse rule data
    automation_rule_t rule = {0};
    cJSON *item;
    
    item = cJSON_GetObjectItem(json, "id");
    int rule_id = item ? item->valueint : -1;
    
    item = cJSON_GetObjectItem(json, "name");
    if (item && cJSON_IsString(item)) {
        strncpy(rule.name, item->valuestring, sizeof(rule.name) - 1);
        rule.name[sizeof(rule.name) - 1] = '\0';
        API_LOG_I("  Name: %s", rule.name);
    }
    
    item = cJSON_GetObjectItem(json, "enabled");
    if (item) {
        rule.enabled = item->valueint;
        API_LOG_I("  Enabled: %d", rule.enabled);
    }
    
    item = cJSON_GetObjectItem(json, "logic_type");
    if (item) {
        rule.logic_type = item->valueint;
        API_LOG_I("  Logic type: %d", rule.logic_type);
    }
    
    item = cJSON_GetObjectItem(json, "cooldown_seconds");
    if (item) {
        rule.cooldown_seconds = item->valueint;
        rule.cooldown_enabled = (rule.cooldown_seconds > 0);
        API_LOG_I("  Cooldown: %d", rule.cooldown_seconds);
    }
    
    item = cJSON_GetObjectItem(json, "email_recipient");
    if (item && cJSON_IsString(item)) {
        strncpy(rule.email_recipient, item->valuestring, sizeof(rule.email_recipient) - 1);
        rule.email_recipient[sizeof(rule.email_recipient) - 1] = '\0';
    }
    
    item = cJSON_GetObjectItem(json, "email_subject");
    if (item && cJSON_IsString(item)) {
        strncpy(rule.email_subject, item->valuestring, sizeof(rule.email_subject) - 1);
        rule.email_subject[sizeof(rule.email_subject) - 1] = '\0';
    }
    
    // ============================================================
    // FIX: Parse conditions with proper limits
    // ============================================================
    cJSON *conditions = cJSON_GetObjectItem(json, "conditions");
    if (conditions && cJSON_IsArray(conditions)) {
        int cond_count = cJSON_GetArraySize(conditions);
        // Limit to 4 conditions (or MAX_CONDITIONS if defined)
        int max_cond = 4;
        #ifdef MAX_CONDITIONS_PER_RULE
            max_cond = MAX_CONDITIONS_PER_RULE;
        #elif defined(MAX_CONDITIONS)
            max_cond = MAX_CONDITIONS;
        #endif
        rule.condition_count = (cond_count > max_cond) ? max_cond : cond_count;
        API_LOG_I("  Conditions: %d", rule.condition_count);
        
        for (int i = 0; i < rule.condition_count; i++) {
            cJSON *cond = cJSON_GetArrayItem(conditions, i);
            if (cond) {
                cJSON *sensor_id = cJSON_GetObjectItem(cond, "sensor_id");
                cJSON *comparator = cJSON_GetObjectItem(cond, "comparator");
                cJSON *threshold = cJSON_GetObjectItem(cond, "threshold");
                
                if (sensor_id) {
                    rule.conditions[i].sensor_id = sensor_id->valueint;
                    API_LOG_I("    Cond %d: sensor_id=%d", i, rule.conditions[i].sensor_id);
                }
                if (comparator) {
                    rule.conditions[i].comparator = comparator->valueint;
                }
                if (threshold) {
                    rule.conditions[i].threshold = threshold->valuedouble;
                }
            }
        }
    } else {
        API_LOG_I("  No conditions found");
    }

    cJSON *outputs = cJSON_GetObjectItem(json, "outputs");
    if (outputs && cJSON_IsArray(outputs)) {
        int out_count = cJSON_GetArraySize(outputs);
        // Limit to 4 outputs (or MAX_OUTPUTS if defined)
        int max_out = 4;
        #ifdef MAX_OUTPUTS_PER_RULE
            max_out = MAX_OUTPUTS_PER_RULE;
        #elif defined(MAX_OUTPUTS)
            max_out = MAX_OUTPUTS;
        #endif
        rule.output_count = (out_count > max_out) ? max_out : out_count;
        API_LOG_I("  Outputs: %d", rule.output_count);
        
        for (int i = 0; i < rule.output_count; i++) {
            cJSON *out = cJSON_GetArrayItem(outputs, i);
            if (out) {
                cJSON *type = cJSON_GetObjectItem(out, "type");
                cJSON *id = cJSON_GetObjectItem(out, "id");
                cJSON *duration = cJSON_GetObjectItem(out, "duration");
                
                if (type) {
                    rule.outputs[i].type = type->valueint;
                    API_LOG_I("    Out %d: type=%d", i, rule.outputs[i].type);
                }
                if (id) {
                    rule.outputs[i].id = id->valueint;
                    API_LOG_I("    Out %d: id=%d", i, rule.outputs[i].id);
                }
                if (duration) {
                    rule.outputs[i].custom_duration = duration->valueint;
                }
            }
        }
    } else {
        API_LOG_I("  No outputs found");
    }
    
    // ============================================================
    // Save the rule
    // ============================================================
    int result = -1;
    int new_rule_id = -1;
    
    if (rule_id >= 0) {
        // Update existing rule
        result = rule_update(rule_id, &rule);
        new_rule_id = rule_id;
        API_LOG_I("  Updating rule %d", rule_id);
    } else {
        // Create new rule
        result = rule_create(&rule);
        new_rule_id = result;
        API_LOG_I("  Creating new rule, result: %d", result);
    }
    
    cJSON_Delete(json);
    
    if (result >= 0) {
        char response[128];
        snprintf(response, sizeof(response), 
                 "{\"status\":\"success\",\"rule_id\":%d,\"action\":\"%s\"}",
                 new_rule_id, (rule_id >= 0) ? "updated" : "created");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        API_LOG_I("  Rule saved successfully: %d", new_rule_id);
        return ESP_OK;
    } else {
        API_LOG_E("  Failed to save rule, result: %d", result);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save rule");
        return ESP_FAIL;
    }
}

// ============================================================
// DELETE /api/rules/delete?id={id} - Delete a rule
// ============================================================
esp_err_t api_rules_delete_handler(httpd_req_t *req) {
    char query[128] = {0};
    char id_str[16] = {0};
    int rule_id = -1;
    
    // Get the query string
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "id", id_str, sizeof(id_str)) == ESP_OK) {
            rule_id = atoi(id_str);
        }
    }
    
    API_LOG_I("DELETE /api/rules/delete - ID: %d", rule_id);
    
    if (rule_id < 0 || rule_id >= MAX_RULES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid rule ID");
        return ESP_FAIL;
    }
    
    int result = rule_delete(rule_id);
    if (result == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"success\"}", -1);
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete rule");
        return ESP_FAIL;
    }
}

// ============================================================
// GET /api/rules/export - Export rules as JSON
// ============================================================
esp_err_t api_rules_export_handler(httpd_req_t *req) {
    char response[8192];
    int pos = 0;
    automation_rule_t *rules = rule_get_all();
//    int count = rule_get_count();
    int found = 0;
    
    pos += snprintf(response + pos, sizeof(response) - pos, "{\"rules\":[");
    
    for (int i = 0; i < MAX_RULES && pos < (int)sizeof(response) - 200; i++) {
        if (rules[i].name[0] == '\0') continue;
        if (rules[i].name[0] == 0xFF) continue;
        
        if (found > 0) {
            pos += snprintf(response + pos, sizeof(response) - pos, ",");
        }
        found++;
        
        // Build conditions JSON
        char conditions_str[512] = "";
        int cond_pos = 0;
        for (int j = 0; j < rules[i].condition_count && j < 4; j++) {
            if (j > 0) cond_pos += snprintf(conditions_str + cond_pos, sizeof(conditions_str) - cond_pos, ",");
            cond_pos += snprintf(conditions_str + cond_pos, sizeof(conditions_str) - cond_pos,
                "{\"sensor_id\":%d,\"comparator\":%d,\"threshold\":%.2f}",
                rules[i].conditions[j].sensor_id,
                rules[i].conditions[j].comparator,
                rules[i].conditions[j].threshold
            );
        }
        
        // Build outputs JSON
        char outputs_str[512] = "";
        int out_pos = 0;
        for (int j = 0; j < rules[i].output_count && j < 4; j++) {
            if (j > 0) out_pos += snprintf(outputs_str + out_pos, sizeof(outputs_str) - out_pos, ",");
            out_pos += snprintf(outputs_str + out_pos, sizeof(outputs_str) - out_pos,
                "{\"type\":%d,\"id\":%d,\"duration\":%d}",
                rules[i].outputs[j].type,
                rules[i].outputs[j].id,
                rules[i].outputs[j].custom_duration
            );
        }
        
        pos += snprintf(response + pos, sizeof(response) - pos,
            "{"
            "\"id\":%d,"
            "\"name\":\"%s\","
            "\"enabled\":%d,"
            "\"logic_type\":%d,"
            "\"condition_count\":%d,"
            "\"conditions\":[%s],"
            "\"output_count\":%d,"
            "\"outputs\":[%s],"
            "\"cooldown_seconds\":%d,"
            "\"email_recipient\":\"%s\","
            "\"email_subject\":\"%s\""
            "}",
            rules[i].rule_id,
            rules[i].name,
            rules[i].enabled,
            rules[i].logic_type,
            rules[i].condition_count,
            conditions_str,
            rules[i].output_count,
            outputs_str,
            rules[i].cooldown_seconds,
            rules[i].email_recipient,
            rules[i].email_subject
        );
    }
    
    snprintf(response + pos, sizeof(response) - pos, "],\"count\":%d}", found);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"rules_export.json\"");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// ============================================================
// POST /api/rules/import - Import rules from JSON
// ============================================================
esp_err_t api_rules_import_handler(httpd_req_t *req) {
    char buffer[8192];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON data");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *rules_array = cJSON_GetObjectItem(root, "rules");
    if (!rules_array || !cJSON_IsArray(rules_array)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'rules' array");
        return ESP_FAIL;
    }
    
    int imported = 0;
    int array_size = cJSON_GetArraySize(rules_array);
    
    for (int i = 0; i < array_size; i++) {
        cJSON *rule_json = cJSON_GetArrayItem(rules_array, i);
        if (!rule_json) continue;
        
        automation_rule_t rule = {0};
        
        // Parse basic fields
        cJSON *item;
        item = cJSON_GetObjectItem(rule_json, "name");
        if (item && cJSON_IsString(item)) {
            strncpy(rule.name, item->valuestring, sizeof(rule.name) - 1);
        }
        
        item = cJSON_GetObjectItem(rule_json, "enabled");
        if (item) rule.enabled = item->valueint;
        
        item = cJSON_GetObjectItem(rule_json, "logic_type");
        if (item) rule.logic_type = item->valueint;
        
        item = cJSON_GetObjectItem(rule_json, "cooldown_seconds");
        if (item) rule.cooldown_seconds = item->valueint;
        rule.cooldown_enabled = (rule.cooldown_seconds > 0);
        
        item = cJSON_GetObjectItem(rule_json, "email_recipient");
        if (item && cJSON_IsString(item)) {
            strncpy(rule.email_recipient, item->valuestring, sizeof(rule.email_recipient) - 1);
        }
        
        item = cJSON_GetObjectItem(rule_json, "email_subject");
        if (item && cJSON_IsString(item)) {
            strncpy(rule.email_subject, item->valuestring, sizeof(rule.email_subject) - 1);
        }
        
        // Parse conditions
        cJSON *conditions = cJSON_GetObjectItem(rule_json, "conditions");
        if (conditions && cJSON_IsArray(conditions)) {
            int cond_count = cJSON_GetArraySize(conditions);
            rule.condition_count = cond_count > 4 ? 4 : cond_count;
            for (int j = 0; j < rule.condition_count; j++) {
                cJSON *cond = cJSON_GetArrayItem(conditions, j);
                if (cond) {
                    cJSON *sensor_id = cJSON_GetObjectItem(cond, "sensor_id");
                    cJSON *comparator = cJSON_GetObjectItem(cond, "comparator");
                    cJSON *threshold = cJSON_GetObjectItem(cond, "threshold");
                    
                    if (sensor_id) rule.conditions[j].sensor_id = sensor_id->valueint;
                    if (comparator) rule.conditions[j].comparator = comparator->valueint;
                    if (threshold) rule.conditions[j].threshold = threshold->valuedouble;
                }
            }
        }
        
        // Parse outputs
        cJSON *outputs = cJSON_GetObjectItem(rule_json, "outputs");
        if (outputs && cJSON_IsArray(outputs)) {
            int out_count = cJSON_GetArraySize(outputs);
            rule.output_count = out_count > 4 ? 4 : out_count;
            for (int j = 0; j < rule.output_count; j++) {
                cJSON *out = cJSON_GetArrayItem(outputs, j);
                if (out) {
                    cJSON *type = cJSON_GetObjectItem(out, "type");
                   cJSON *id = cJSON_GetObjectItem(out, "id");
                    cJSON *duration = cJSON_GetObjectItem(out, "duration");
                    
                    if (type) rule.outputs[j].type = type->valueint;
                    if (id) rule.outputs[j].id = id->valueint;
                    if (duration) rule.outputs[j].custom_duration = duration->valueint;
                }
            }
        }
        
        // Save the rule
        int result = rule_create(&rule);
        if (result >= 0) {
            imported++;
        }
    }
    
    cJSON_Delete(root);
    
    char response[128];
    snprintf(response, sizeof(response), 
             "{\"status\":\"success\",\"imported\":%d,\"total\":%d}", 
             imported, array_size);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// ============================================================
// GET /api/history - Get historical sensor data
// ============================================================
esp_err_t api_get_history_handler(httpd_req_t *req)
{
    // CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Handle preflight OPTIONS
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // Get parameters
    char limit_str[8];
    char start_str[16] = "0";
    char end_str[16] = "0";
    
    snprintf(limit_str, sizeof(limit_str), "%d", HISTORY_MAX_RECORDS_PER_PAGE);
    // Get query string
    char query[128] = {0};
    size_t query_len = httpd_req_get_url_query_len(req);
    API_LOG_I("Query length: %d", query_len);
    
    if (query_len > 0) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            API_LOG_I("Full query string: %s", query);
            
            // Parse parameters with better error checking
            if (httpd_query_key_value(query, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
                API_LOG_I("Parsed limit: %s", limit_str);
            } else {
                API_LOG_I("No limit parameter found, using default: 60");
            }
            
            httpd_query_key_value(query, "start", start_str, sizeof(start_str));
            httpd_query_key_value(query, "end", end_str, sizeof(end_str));
        }
    } else {
        API_LOG_I("No query string, using defaults");
    }
    
    int limit = atoi(limit_str);
    if (limit < 1) limit = 1;
    if (limit > 4320) limit = 4320;
    
    API_LOG_I("Final limit: %d", limit);  // Debug
    
    uint32_t start_ts = atoi(start_str);
    uint32_t end_ts = atoi(end_str);
    
    // If no time range specified, get latest records
    if (start_ts == 0 && end_ts == 0) {

        // Get the newest records by reading backwards from end
        uint32_t total_records = sensor_history_get_record_count();
        if (total_records == 0) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"entries\":[]}", -1);
            return ESP_OK;
        }

        // We need to get the most recent 'limit' records
        // Since we can only get by timestamp range, we need to get the newest timestamp
        uint32_t newest_ts = sensor_history_get_newest_ts();
        uint32_t oldest_ts = sensor_history_get_oldest_ts();

        if (newest_ts == 0) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"entries\":[]}", -1);
            return ESP_OK;
        }

        // Estimate time range to get enough records
        // If we have enough records, go back estimated time
        uint32_t time_range = HISTORY_MAX_RECORDS_PER_PAGE;
        if (total_records > 1 && newest_ts > oldest_ts) {
            uint32_t avg_interval = (newest_ts - oldest_ts) / (total_records - 1);
            time_range = avg_interval * limit * 2;
            if (time_range < HISTORY_MAX_RECORDS_PER_PAGE) time_range = HISTORY_MAX_RECORDS_PER_PAGE;
        }

        start_ts = newest_ts - time_range;
        end_ts = newest_ts + 1;
    }

    #define CHUNK_SIZE 50  // Read 50 records at a time
    
    // Start JSON response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"entries\":[", -1);
    
    // Variables for chunked reading
    sensor_record_t records[CHUNK_SIZE];
    int total_sent = 0;
    int first_chunk = 1;
    uint32_t current_start_ts = start_ts;
    uint32_t current_end_ts = end_ts;
    int records_remaining = limit;
    
    // Load sensor configs once (for name mapping)
    sensor_config_t sensor_config[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(sensor_config, TOTAL_SENSOR_COUNT);
    
    // Keep reading in chunks until we have enough records
    while (records_remaining > 0) {
        int batch_size = (records_remaining < CHUNK_SIZE) ? records_remaining : CHUNK_SIZE;
        
        // Read a batch of records
        int count = sensor_history_get_range(current_start_ts, current_end_ts, records, batch_size);
        
        if (count == 0) {
            // No more records available
            break;
        }
        
        // Send each record in the batch
        for (int i = 0; i < count && total_sent < limit; i++) {
            char record_str[512];
            int pos = 0;
            
            // Add comma between records (except first)
            if (!first_chunk) {
                pos += snprintf(record_str + pos, sizeof(record_str) - pos, ",");
            }
            first_chunk = 0;
            
            // Build the record JSON
            pos += snprintf(record_str + pos, sizeof(record_str) - pos,
                "{\"timestamp\":%lu,\"sensor_mask\":%u",
                records[i].timestamp,
                records[i].sensor_mask);
            
            // Add each sensor value using configured names
            for (int j = 0; j < TOTAL_SENSOR_COUNT; j++) {
                // Use configured name if available, otherwise default
                const char *name = sensor_config[j].name;
                if (!name || name[0] == '\0' || name[0] == 0xFF) {
                    name = default_sensor_names[j];
                }
                
                // Sanitize name for JSON
                char key[32];
                strncpy(key, name, sizeof(key) - 1);
                key[sizeof(key) - 1] = '\0';
                for (char *p = key; *p; p++) {
                    if (*p == ' ') *p = '_';
                }
                
                pos += snprintf(record_str + pos, sizeof(record_str) - pos,
                    ",\"%s\":%u",
                    key,
                    records[i].values[j]);
            }
            
            pos += snprintf(record_str + pos, sizeof(record_str) - pos, "}");
            
            // Send this chunk
            esp_err_t err = httpd_resp_send_chunk(req, record_str, pos);
            if (err != ESP_OK) {
                API_LOG_E("Failed to send record chunk: %d", err);
                return err;
            }
            
            total_sent++;
            records_remaining--;
        }
        
        // Update time range for next batch
        if (count > 0) {
            // Move start_ts forward to the last timestamp we read
            current_start_ts = records[count - 1].timestamp + 1;
        } else {
            break;
        }
        
        // Small delay to allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Send closing bracket
    esp_err_t err = httpd_resp_send_chunk(req, "]}", -1);
    if (err != ESP_OK) {
        API_LOG_E("Failed to send closing chunk: %d", err);
        return err;
    }
    
    // Send final empty chunk to signal end of response
    err = httpd_resp_send_chunk(req, NULL, 0);
    if (err != ESP_OK) {
        API_LOG_E("Failed to send final chunk: %d", err);
    }
    
    API_LOG_I("Returned %d history records in chunks", total_sent);
    return ESP_OK;
}

// ============================================================
// GET /api/history/config - Get sensor configuration
// ============================================================
esp_err_t api_get_sensor_config_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    const char* chart_colors[16] = {
        "#00FFFF",  // Cyan / Aqua
        "#FF00FF",  // Magenta / Fuchsia
        "#FFFF00",  // Neon Yellow
        "#39FF14",  // Neon Green
        "#FF3366",  // Hot Pink
        "#FF6600",  // Vivid Orange
        "#3399FF",  // Sky Blue
        "#B8860B",  // Dark Goldenrod
        "#9932CC",  // Dark Orchid
        "#00FF7F",  // Spring Green
        "#FFD700",  // Gold
        "#FF4500",  // Orange Red
        "#8A2BE2",  // Blue Violet
        "#ADFF2F",  // Green Yellow
        "#F08080",  // Light Coral
        "#E6E6FA"   // Lavender / Off-White
    };

    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send(req, "{\"error\":\"Memory error\"}", -1);
        return ESP_OK;
    }
    
    cJSON *sensors_array = cJSON_CreateArray();
    if (!sensors_array) {
        cJSON_Delete(root);
        httpd_resp_send(req, "{\"error\":\"Memory error\"}", -1);
        return ESP_OK;
    }

    sensor_config_t configs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(configs, TOTAL_SENSOR_COUNT);

    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        cJSON *sensor_obj = cJSON_CreateObject();
        if (!sensor_obj) continue;
        
        const char *name = configs[i].name;
        
        cJSON_AddNumberToObject(sensor_obj, "id", i);
        cJSON_AddStringToObject(sensor_obj, "name", name);
        cJSON_AddStringToObject(sensor_obj, "color", chart_colors[i]);
        
        cJSON_AddItemToArray(sensors_array, sensor_obj);
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors_array);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, -1);
        free(json_str);
    } else {
        httpd_resp_send(req, "{\"error\":\"JSON error\"}", -1);
    }
    
    return ESP_OK;
}

// ============================================================
// GET /api/history/export - Export CSV
// ============================================================
esp_err_t api_export_csv_handler(httpd_req_t *req)
{
    // CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    // Get parameters
    char limit_str[8];
    snprintf(limit_str, sizeof(limit_str), "%d", HISTORY_MAX_RECORDS_PER_PAGE);
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        char query[64];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            httpd_query_key_value(query, "limit", limit_str, sizeof(limit_str));
        }
    }
    
    int limit = atoi(limit_str);
    if (limit < 1) limit = 1;
    if (limit > HISTORY_MAX_RECORDS_PER_PAGE) limit = HISTORY_MAX_RECORDS_PER_PAGE;
    
    // Set CSV content type and download header
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=sensor_history.csv");
    
    // Get records
    uint32_t newest_ts = sensor_history_get_newest_ts();
    if (newest_ts == 0) {
        httpd_resp_send(req, "No data available", -1);
        return ESP_OK;
    }
    
    // Go back enough to get 'limit' records
    uint32_t total = sensor_history_get_record_count();
    uint32_t oldest_ts = sensor_history_get_oldest_ts();
    uint32_t time_range = 0;
    if (total > 1 && newest_ts > oldest_ts) {
        uint32_t avg_interval = (newest_ts - oldest_ts) / (total - 1);
        time_range = avg_interval * limit * 2;
    }
    if (time_range < HISTORY_MAX_RECORDS_PER_PAGE) time_range = HISTORY_MAX_RECORDS_PER_PAGE;
    
    uint32_t start_ts = newest_ts - time_range;
    uint32_t end_ts = newest_ts + 1;
    
    sensor_record_t *records = malloc(limit * sizeof(sensor_record_t));
    if (!records) {
        httpd_resp_send(req, "Memory allocation failed", -1);
        return ESP_OK;
    }
    
    int count = sensor_history_get_range(start_ts, end_ts, records, limit);
    
    sensor_config_t sensor_config[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(sensor_config, TOTAL_SENSOR_COUNT);

    char *result = malloc((sizeof(sensor_config[0].name)+2)*TOTAL_SENSOR_COUNT); //+2 for comma&space for each entry and null terminator at the end.
    if (!result) return 1;
    
    // Build the string
    char *ptr = result;
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        ptr += sprintf(ptr, "%s", sensor_config[i].name);
        // Add separator
        if (i < (TOTAL_SENSOR_COUNT - 1)) ptr += sprintf(ptr, ", ");
    }
    ptr += sprintf(ptr, "\n");
    // Write CSV header
    httpd_resp_send_chunk(req, ptr, -1);
    
    // Write each record
    char line[512];
    int pos = 0;
    
    for (int i = 0; i < count; i++) {
        sensor_record_t *r = &records[i];
        
        // Format timestamp
        char ts_str[32];
        time_t ts = r->timestamp;
        struct tm *tm_info = localtime(&ts);
        strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        pos += sprintf(line + pos, "%s ", ts_str);
        // Build CSV line
        for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
            pos += sprintf(line + pos, "%d", r->values[i]);
            if (i < (TOTAL_SENSOR_COUNT - 1)) pos += sprintf(line + pos, ", ");
        }
    }
    
    free(records);
    httpd_resp_send_chunk(req, NULL, 0);  // End chunked response
    
    API_LOG_I("Exported %d records as CSV", count);
    return ESP_OK;
}

// ============================================================
// GET /api/history/stats - Get history statistics
// ============================================================
esp_err_t api_get_history_stats_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send(req, "{\"error\":\"Memory error\"}", -1);
        return ESP_OK;
    }
    
    cJSON_AddNumberToObject(root, "record_count", sensor_history_get_record_count());
    cJSON_AddNumberToObject(root, "max_records", HISTORY_MAX_RECORDS_PER_PAGE);
    cJSON_AddNumberToObject(root, "oldest_timestamp", sensor_history_get_oldest_ts());
    cJSON_AddNumberToObject(root, "newest_timestamp", sensor_history_get_newest_ts());
    cJSON_AddNumberToObject(root, "sample_interval", 60);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, -1);
        free(json_str);
    } else {
        httpd_resp_send(req, "{\"error\":\"JSON error\"}", -1);
    }
    
    return ESP_OK;
}

// ============================================================
// GET-POST /api/email/config - Get & Update email configuration
// ============================================================
esp_err_t email_config_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        email_config_t config;
        email_load_config(&config);
        
        char response[1024];
        snprintf(response, sizeof(response),
            "{\n"
            "  \"enabled\": %s,\n"
            "  \"smtp_server\": \"%s\",\n"
            "  \"smtp_port\": %d,\n"
            "  \"username\": \"%s\",\n"
            "  \"password\": \"%s\",\n"
            "  \"from_email\": \"%s\",\n"
            "  \"to_emails\": \"%s\",\n"
            "  \"use_tls\": %s\n"
            "}",
            config.enabled ? "true" : "false",
            config.smtp_server,
            config.smtp_port,
            config.username,
            "********", // Don't send real password
            config.from_email,
            config.to_emails,
            config.use_tls ? "true" : "false"
        );
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
        return ESP_OK;
    }
    
    // POST: Update email config
    if (req->method == HTTP_POST) {
        char buf[1024] = {0};
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        // Parse JSON and update config
        email_config_t config;

        cJSON *json = cJSON_Parse(buf);
        if (!json) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        cJSON *item;

        item = cJSON_GetObjectItem(json, "enabled");
        if (item && cJSON_IsBool(item)) {
            config.enabled = item->valueint;
        }
        
        item = cJSON_GetObjectItem(json, "smtp_server");
        if (item && cJSON_IsString(item)) {
            strncpy(config.smtp_server, item->valuestring, sizeof(config.smtp_server) - 1);
            config.smtp_server[sizeof(config.smtp_server) - 1] = '\0';
        }

        item = cJSON_GetObjectItem(json, "smtp_port");
        if (item && cJSON_IsNumber(item)) {
            config.smtp_port = item->valueint;
        }

        item = cJSON_GetObjectItem(json, "username");
        if (item && cJSON_IsString(item)) {
            strncpy(config.username, item->valuestring, sizeof(config.username) - 1);
            config.username[sizeof(config.username) - 1] = '\0';
        }

        item = cJSON_GetObjectItem(json, "password");
        if (item && cJSON_IsString(item)) {
            strncpy(config.password, item->valuestring, sizeof(config.password) - 1);
            config.password[sizeof(config.password) - 1] = '\0';
        }

        item = cJSON_GetObjectItem(json, "from_email");
        if (item && cJSON_IsString(item)) {
            strncpy(config.from_email, item->valuestring, sizeof(config.from_email) - 1);
            config.from_email[sizeof(config.from_email) - 1] = '\0';
        }

        item = cJSON_GetObjectItem(json, "from_email");
        if (item && cJSON_IsString(item)) {
            strncpy(config.to_emails, item->valuestring, sizeof(config.to_emails) - 1);
            config.to_emails[sizeof(config.to_emails) - 1] = '\0';
        }

        item = cJSON_GetObjectItem(json, "use_tls");
        if (item && cJSON_IsBool(item)) {
            config.use_tls = item->valueint;
        }

        email_save_config(&config);
        httpd_resp_send(req, "{\"status\":\"ok\"}", strlen("{\"status\":\"ok\"}"));
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Either GET or POST is allowed");
    return ESP_OK;
}

// ============================================================
// GET /api/email/test - Get history statistics
// ============================================================
// Test email endpoint
esp_err_t email_test_handler(httpd_req_t *req) {
    esp_err_t err = email_send_test();
    if (err == ESP_OK) {
        httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Test email sent\"}", 
                       strlen("{\"status\":\"ok\",\"message\":\"Test email sent\"}"));
    } else {
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Failed to send test email\"}", 
                       strlen("{\"status\":\"error\",\"message\":\"Failed to send test email\"}"));
    }
    return ESP_OK;
}

esp_err_t api_adc_pin_mapping_get_handler(httpd_req_t *req) {
    const char *json = adc_get_pin_mapping_json();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

/**
 * @brief Handler for /api/sensor/gpio - Get GPIO info for a sensor
 */
esp_err_t api_sensor_gpio_info_get_handler(httpd_req_t *req) {
    char query[128] = {0};
    char sensor_name[64] = {0};
    const char *gpio_info = "N/A";
    
    // Get the query string - returns esp_err_t, not a pointer
    esp_err_t err = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (err == ESP_OK && query[0] != '\0') {
        // Parse the sensor parameter manually
        char *param = strstr(query, "sensor=");
        if (param) {
            param += 7;  // Skip "sensor="
            char *end = strchr(param, '&');
            if (end) {
                int len = end - param;
                if (len < (int)sizeof(sensor_name)) {
                    strncpy(sensor_name, param, len);
                    sensor_name[len] = '\0';
                }
            } else {
                strncpy(sensor_name, param, sizeof(sensor_name) - 1);
                sensor_name[sizeof(sensor_name) - 1] = '\0';
            }
            
            // URL decode: replace + with space
            for (int i = 0; sensor_name[i]; i++) {
                if (sensor_name[i] == '+') sensor_name[i] = ' ';
            }
            
            gpio_info = sensor_get_gpio_info(sensor_name);
        }
    }
    
    // Build JSON response
    char response[128];
    snprintf(response, sizeof(response), "{\"sensor\":\"%s\",\"gpio\":\"%s\"}", 
             sensor_name[0] ? sensor_name : "unknown", gpio_info);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// ============================================================
// Register all API endpoints
// ============================================================
void register_api_endpoints(httpd_handle_t server) {
    // System endpoints
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
    
    httpd_uri_t echo_uri = {
        .uri = "/api/echo",
        .method = HTTP_GET,
        .handler = echo_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &echo_uri);
    
    // Sensor endpoints
    httpd_uri_t sensors_uri = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = sensors_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sensors_uri);
    
    httpd_uri_t sensors_config_get_uri = {
        .uri = "/api/sensors/config",
        .method = HTTP_GET,
        .handler = sensors_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sensors_config_get_uri);
    
    httpd_uri_t sensors_config_post_uri = {
        .uri = "/api/sensors/config",
        .method = HTTP_POST,
        .handler = sensors_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sensors_config_post_uri);
    
    // Relay endpoints
    httpd_uri_t relays_uri = {
        .uri = "/api/relays",
        .method = HTTP_GET,
        .handler = relays_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &relays_uri);
    
    httpd_uri_t relay_trigger_uri = {
        .uri = "/api/relays/trigger",
        .method = HTTP_POST,
        .handler = relay_trigger_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &relay_trigger_uri);
    
    httpd_uri_t relay_off_uri = {
        .uri = "/api/relays/off",
        .method = HTTP_POST,
        .handler = relay_off_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &relay_off_uri);
    
    httpd_uri_t relays_config_get_uri = {
        .uri = "/api/relays/config",
        .method = HTTP_GET,
        .handler = relays_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &relays_config_get_uri);
    
    httpd_uri_t relays_config_post_uri = {
        .uri = "/api/relays/config",
        .method = HTTP_POST,
        .handler = relays_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &relays_config_post_uri);
    
    // Config endpoints
    httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &config_get_uri);
    
    httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &config_post_uri);
    
    // WiFi endpoints
    httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_scan_uri);
    
    httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = wifi_connect_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_connect_uri);
    
    httpd_uri_t wifi_forget_uri = {
        .uri = "/api/wifi/forget",
        .method = HTTP_POST,
        .handler = wifi_forget_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_forget_uri);
    
    httpd_uri_t wifi_status_uri = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = wifi_status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_status_uri);
    
    // Calibration endpoints
    httpd_uri_t cal_start_uri = {
        .uri = "/api/calibrate/start",
        .method = HTTP_POST,
        .handler = calibrate_start_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_start_uri);
    
    httpd_uri_t cal_sample_uri = {
        .uri = "/api/calibrate/sample",
        .method = HTTP_POST,
        .handler = calibrate_sample_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_sample_uri);
    
    httpd_uri_t cal_apply_uri = {
        .uri = "/api/calibrate/apply",
        .method = HTTP_POST,
        .handler = calibrate_apply_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_apply_uri);
    
    httpd_uri_t cal_cancel_uri = {
        .uri = "/api/calibrate/cancel",
        .method = HTTP_POST,
        .handler = calibrate_cancel_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_cancel_uri);
    
    httpd_uri_t cal_factor_uri = {
        .uri = "/api/calibrate/factor",
        .method = HTTP_GET,
        .handler = calibrate_factor_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_factor_uri);
    
    // NTP endpoints
    httpd_uri_t ntp_sync_uri = {
        .uri = "/api/ntp/sync",
        .method = HTTP_POST,
        .handler = ntp_sync_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &ntp_sync_uri);
    
    httpd_uri_t ntp_manual_uri = {
        .uri = "/api/ntp/manual",
        .method = HTTP_POST,
        .handler = ntp_manual_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &ntp_manual_uri);
    
    // MODBUS endpoints
    httpd_uri_t modbus_map_get_uri = {
        .uri = "/api/modbus/map",
        .method = HTTP_GET,
        .handler = modbus_map_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &modbus_map_get_uri);
    
    httpd_uri_t modbus_map_post_uri = {
        .uri = "/api/modbus/map",
        .method = HTTP_POST,
        .handler = modbus_map_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &modbus_map_post_uri);
    
    // Reboot endpoint
    httpd_uri_t reboot_uri = {
        .uri = "/api/system/reboot",
        .method = HTTP_POST,
        .handler = system_reboot_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &reboot_uri);

    // GET /api/logs - list logs OR download specific log
    httpd_uri_t logs_get_uri = {
        .uri = "/api/logs",
        .method = HTTP_GET,
        .handler = logs_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &logs_get_uri);

    // DELETE /api/logs - delete specific log
    httpd_uri_t logs_delete_uri = {
        .uri = "/api/logs",
        .method = HTTP_DELETE,
        .handler = logs_delete_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &logs_delete_uri);

    // DELETE /api/logs/all - delete all logs
    httpd_uri_t logs_delete_all_uri = {
        .uri = "/api/logs/all",
        .method = HTTP_DELETE,
        .handler = logs_delete_handler,  // Same handler, checks URI
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &logs_delete_all_uri);
 
    httpd_uri_t email_config_get_uri = {
        .uri = "/api/email/config",
        .method = HTTP_GET,
        .handler = email_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &email_config_get_uri);

    httpd_uri_t email_config_post_uri = {
        .uri = "/api/email/config",
        .method = HTTP_POST,
        .handler = email_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &email_config_post_uri);

    httpd_uri_t email_test_uri = {
        .uri = "/api/email/test",
        .method = HTTP_POST,
        .handler = email_test_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &email_test_uri);

    // GET /api/rules
    httpd_uri_t rules_get_uri = {
        .uri       = "/api/rules",
        .method    = HTTP_GET,
        .handler   = api_rules_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &rules_get_uri);

    // POST /api/rules
    httpd_uri_t rules_post_uri = {
        .uri       = "/api/rules",
        .method    = HTTP_POST,
        .handler   = api_rules_post_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &rules_post_uri);

    // GET /api/rules/export
    httpd_uri_t rules_export_uri = {
        .uri       = "/api/rules/export",
        .method    = HTTP_GET,
        .handler   = api_rules_export_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &rules_export_uri);

    // POST /api/rules/import
    httpd_uri_t rules_import_uri = {
        .uri       = "/api/rules/import",
        .method    = HTTP_POST,
        .handler   = api_rules_import_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &rules_import_uri);

    // GET /api/history
    httpd_uri_t get_history_uri = {
        .uri       = "/api/history",
        .method    = HTTP_GET,
        .handler   = api_get_history_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &get_history_uri);

    // GET /api/history/config - returns sensor configuration
    httpd_uri_t history_config_uri = {
        .uri       = "/api/history/config",
        .method    = HTTP_GET,
        .handler   = api_get_sensor_config_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &history_config_uri);

    // GET /api/history/stats - returns statistics
    httpd_uri_t history_stats_uri = {
        .uri       = "/api/history/stats",
        .method    = HTTP_GET,
        .handler   = api_get_history_stats_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &history_stats_uri);

    // GET /api/history/export - exports sensor data
    httpd_uri_t history_export_uri = {
        .uri       = "/api/history/export",
        .method    = HTTP_GET,
        .handler   = api_export_csv_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &history_export_uri);

    // DELETE /api/rules/* 
    httpd_uri_t rules_delete_uri = {
        .uri       = "/api/rules/delete",
        .method    = HTTP_DELETE,
        .handler   = api_rules_delete_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &rules_delete_uri);

    httpd_uri_t api_adc_pin_mapping_uri = {
        .uri       = "/api/adc/pins",
        .method    = HTTP_GET,
        .handler   = api_adc_pin_mapping_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_adc_pin_mapping_uri);

     httpd_uri_t api_sensor_gpio_info_uri = {
        .uri       = "/api/sensor/gpio",
        .method    = HTTP_GET,
        .handler   = api_sensor_gpio_info_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &api_sensor_gpio_info_uri);

   API_LOG_I("All API endpoints registered");
}