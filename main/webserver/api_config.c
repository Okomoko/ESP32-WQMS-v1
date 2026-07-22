// api_config.c
// Configuration API endpoints - sensor/relay name updates

#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_http_server.h"

#include "api_config.h"
#include "sensor_read.h"
#include "relay_control.h"
#include "nvs_config.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Local Helper Functions (no new external dependencies)
// ============================================================

// -------- Sensor Helpers --------
static uint16_t get_sensor_calibration(int sensor_id) {
    return sensor_get_calibration(sensor_id);
}

static uint8_t get_sensor_gpio(int sensor_id) {
    return sensor_get_gpio(sensor_id);
}

static uint16_t get_sensor_modbus(int sensor_id) {
    return sensor_get_modbus(sensor_id);
}

static float get_sensor_min(int sensor_id) {
    return sensor_get_min(sensor_id);
}

static float get_sensor_max(int sensor_id) {
    return sensor_get_max(sensor_id);
}

// -------- Relay Helpers --------
static uint8_t get_relay_gpio(int relay_id) {
    relay_config_t *r_config;
    r_config = relay_get_config(relay_id);
    return r_config->gpio_pin;
}

static uint16_t get_relay_modbus(int relay_id) {
    relay_config_t *r_config;
    r_config = relay_get_config(relay_id);
    return r_config->modbus_register;
}

static uint16_t get_relay_duration(int relay_id) {
    relay_config_t *r_config;
    r_config = relay_get_config(relay_id);
    return r_config->activity_duration;
}

static uint16_t get_relay_off_delay(int relay_id) {
    relay_config_t *r_config;
    r_config = relay_get_config(relay_id);
    return r_config->off_delay;
}

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
// GET /api/sensors/config
// ============================================================
esp_err_t sensors_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors_array = cJSON_CreateArray();
    
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        sensor_reading_t *reading = sensor_get_reading(i);
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddNumberToObject(sensor, "id", i);
        cJSON_AddStringToObject(sensor, "name", sensor_get_name(i));
        cJSON_AddBoolToObject(sensor, "enabled", reading && reading->status != SENSOR_STATUS_DISABLED);
        cJSON_AddNumberToObject(sensor, "calibration_factor", get_sensor_calibration(i));
        cJSON_AddNumberToObject(sensor, "gpio_pin", get_sensor_gpio(i));
        cJSON_AddNumberToObject(sensor, "modbus_register", get_sensor_modbus(i));
        cJSON_AddNumberToObject(sensor, "min_value", get_sensor_min(i));
        cJSON_AddNumberToObject(sensor, "max_value", get_sensor_max(i));
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
    
    cJSON *sensors = cJSON_GetObjectItem(json, "sensors");
    if (!sensors || !cJSON_IsArray(sensors)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'sensors' array");
        return ESP_FAIL;
    }
    
    sensor_config_t configs[TOTAL_SENSOR_COUNT];
    nvs_load_sensor_config(configs, TOTAL_SENSOR_COUNT);
    
    cJSON *item;
    cJSON_ArrayForEach(item, sensors) {
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *enabled = cJSON_GetObjectItem(item, "enabled");
        cJSON *cal = cJSON_GetObjectItem(item, "calibration_factor");
        
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
// GET /api/relays/config
// ============================================================
esp_err_t relays_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *relays_array = cJSON_CreateArray();
    
    for (int i = 0; i < RELAY_COUNT; i++) {
        relay_config_t *cfg = relay_get_config(i);
        cJSON *relay = cJSON_CreateObject();
        cJSON_AddNumberToObject(relay, "id", i);
        
        // ✅ Use relay_get_config() - NOT relay_get_name() or relay_is_enabled()
        if (cfg) {
            cJSON_AddStringToObject(relay, "name", cfg->name);
            cJSON_AddBoolToObject(relay, "enabled", cfg->enabled);
            cJSON_AddNumberToObject(relay, "gpio_pin", cfg->gpio_pin);
            cJSON_AddNumberToObject(relay, "modbus_register", cfg->modbus_register);
            cJSON_AddNumberToObject(relay, "duration_ms", cfg->activity_duration);
            cJSON_AddNumberToObject(relay, "off_delay_ms", cfg->off_delay);
        } else {
            // Fallback defaults if config not available
            cJSON_AddStringToObject(relay, "name", "Unknown");
            cJSON_AddBoolToObject(relay, "enabled", false);
            cJSON_AddNumberToObject(relay, "gpio_pin", get_relay_gpio(i));
            cJSON_AddNumberToObject(relay, "modbus_register", get_relay_modbus(i));
            cJSON_AddNumberToObject(relay, "duration_ms", get_relay_duration(i));
            cJSON_AddNumberToObject(relay, "off_delay_ms", get_relay_off_delay(i));
        }
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
//    WQMS_LOG_I("Request : %s", buffer);

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

//    WQMS_LOG_I("JSON format is correct.");
    cJSON *relays = cJSON_GetObjectItem(json, "relays");
    if (!relays || !cJSON_IsArray(relays)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'relays' array");
        return ESP_FAIL;
    }

//    WQMS_LOG_I("JSON contains 'relays' array.");

    relay_config_t configs[RELAY_COUNT];
    nvs_load_relay_config(configs, RELAY_COUNT);

//    WQMS_LOG_I("Relays config has been loaded.");

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
                    WQMS_LOG_I("Relay name : %s", configs[idx].name);
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


//    WQMS_LOG_I("Relays config is about to be saved.");
    nvs_save_relay_config(configs, RELAY_COUNT);
//    WQMS_LOG_I("Relays config is saved.");
    cJSON_Delete(json);
//    WQMS_LOG_I("Relays config is about to be reloaded.");
    relay_reload_config();
//    WQMS_LOG_I("Relays config is reloaded.");
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Relay configuration saved");
    send_json_response(req, root);
    return ESP_OK;
}