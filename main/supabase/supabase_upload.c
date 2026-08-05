#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "supabase_upload.h"
#include "nvs_config.h"
#include "sensor_read.h"
#include "sensor_history.h"
#include "logger.h"
#include "log_rotate.h"
#include "wifi_manager.h"
#include "cert_manager.h"

// ============================================================
// Static Variables - Last Uploaded Timestamps (Recovery Pointers)
// ============================================================
static uint64_t last_sensor_upload_ts = 0;    // Track last uploaded sensor timestamp
static uint64_t last_log_upload_ts = 0;       // Track last uploaded log timestamp

static TaskHandle_t upload_task_handle = NULL;
static SemaphoreHandle_t upload_mutex = NULL;
static bool is_initialized = false;
static bool is_running = false;

// Upload status
static bool last_sensor_upload_ok = false;
static bool last_log_upload_ok = false;
static uint64_t last_sensor_upload_time = 0;
static uint64_t last_log_upload_time = 0;

static char supabase_cert_buffer[SSL_CERTIFICATE_MAX_SIZE];

// ============================================================
// HTTP Helper
// ============================================================
static esp_err_t http_post_json(const char *url, const char *api_key, const char *json_data) {
    if (!url || !api_key || !json_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check WiFi
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    API_LOG_D("HTTP Post URL : %s", url);
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 2,
        .keep_alive_count = 3,
        .skip_cert_common_name_check = false,
    };
    
    // Load certificate from NVS using generic manager
    size_t cert_len = 0;
    
    if (cert_manager_has(CERT_TYPE_SUPABASE)) {
        esp_err_t err = cert_manager_load(CERT_TYPE_SUPABASE, supabase_cert_buffer, SSL_CERTIFICATE_MAX_SIZE, &cert_len);
        if (err == ESP_OK && cert_len > 0) {
            supabase_cert_buffer[cert_len]=0;
            supabase_cert_buffer[cert_len+1]=0;
            config.cert_pem = supabase_cert_buffer; 
            config.cert_len = cert_len + 1;
        } else {
			WQMS_LOG_E("Error in certificate load (%d), aborting...", err);
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        WQMS_LOG_D("No Supabase certificate in NVS, aborting...");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    if (!client) {
        WQMS_LOG_E("Failed to initialize HTTP client");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", api_key);
    esp_http_client_set_header(client, "Authorization", api_key);
    esp_http_client_set_header(client, "Prefer", "return=minimal");
    esp_http_client_set_header(client, "Prefer", "resolution=merge-duplicates");
    
    esp_http_client_set_post_field(client, json_data, strlen(json_data));

    WQMS_LOG_D("HTTP client is initialized.");
    esp_err_t err = esp_http_client_perform(client);
    WQMS_LOG_D("HTTP POST is performed.");
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code >= 200 && status_code < 300) {
            WQMS_LOG_V("HTTP POST success: %d", status_code);
        } else {
            WQMS_LOG_W("HTTP POST returned status: %d", status_code);
            err = ESP_ERR_INVALID_RESPONSE;
        }
    } else {
        WQMS_LOG_E("HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// ============================================================
// Sensor Recovery - Uses sensor_history.c
// ============================================================
static int upload_pending_sensors(void) {
    if (!nvs_supabase_is_configured()) {
        return 0;
    }

    char url[256];
    nvs_get_supabase_sensor_url(url, sizeof(url));
    const char *api_key = nvs_get_supabase_api_key();
    if (!api_key || strlen(api_key) == 0) {
        return 0;
    }

    // Get total record count from sensor_history
    uint32_t total_records = sensor_history_get_record_count();
    if (total_records == 0) {
        WQMS_LOG_V("No sensor records in history");
        return 0;
    }
    
    // Get the most recent record's timestamp
    uint32_t newest_ts = sensor_history_get_newest_ts();
    if (newest_ts == 0) {
        return 0;
    }

    // If this is the first upload, upload only the most recent
    if (last_sensor_upload_ts == 0) {
        last_sensor_upload_ts = newest_ts;
    }
    
    // Check if we have new data
    if (newest_ts <= last_sensor_upload_ts) {
        WQMS_LOG_V("No new sensor data (last: %llu, newest: %lu)", 
                   last_sensor_upload_ts, newest_ts);
        return 0;
    }
    
    // Get records after last upload timestamp
    uint32_t start_ts = last_sensor_upload_ts + 1;
    uint32_t end_ts = newest_ts;
    
    // Limit how many we fetch at once
    uint32_t count = end_ts - start_ts;
    if (count > MAX_SENSOR_BATCH_SIZE) count = MAX_SENSOR_BATCH_SIZE;
    
    sensor_record_t *records = malloc(count * sizeof(sensor_record_t));
    if (!records) {
        WQMS_LOG_E("Failed to allocate memory for sensor records");
        return 0;
    }
    
    int fetched = sensor_history_get_range(start_ts, end_ts, records, count);
    if (fetched <= 0) {
        free(records);
        return 0;
    }
    
    WQMS_LOG_I("Found %d pending sensor records to upload", fetched);
    
    // Upload each record
    int uploaded = 0;
    const char *sys_name = nvs_get_system_name();
    
    for (int i = 0; i < fetched; i++) {
        // Build JSON payload for this record
        cJSON *root = cJSON_CreateObject();
        if (!root) continue;
        
        cJSON_AddStringToObject(root, "system", sys_name ? sys_name : "WQMS-System");
        
        char timestamp_str[32];
        time_t ts = records[i].timestamp;
        struct tm *tm_info = localtime(&ts);
        strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        cJSON_AddStringToObject(root, "timestamp", timestamp_str);
        
        // Add all sensor values
        for (int j = 0; j < TOTAL_SENSOR_COUNT; j++) {
            char key[16];
            snprintf(key, sizeof(key), "sensor%d", j);
            cJSON_AddNumberToObject(root, key, round(records[i].values[j]*1000.0)/1000.0);
        }
        
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        
        if (json_str) {
            esp_err_t err = http_post_json(url, api_key, json_str);
            free(json_str);
            
            if (err == ESP_OK) {
                uploaded++;
                last_sensor_upload_ts = records[i].timestamp;
                last_sensor_upload_ok = true;
                last_sensor_upload_time = esp_timer_get_time() / 1000000ULL;
                WQMS_LOG_V("Uploaded sensor record timestamp: %lu", records[i].timestamp);
            } else {
                WQMS_LOG_W("Failed to upload sensor record, will retry later");
                break;  // Stop on failure, retry later
            }
        }
    }
    
    free(records);
    return uploaded;
}

// ============================================================
// Log Recovery - LINE-BASED reading from log_rotate (SPIFFS)
// ============================================================
static int upload_pending_logs(void) {
    typedef struct {
        uint64_t log_ts;
        char timestamp_str[32];
        char module[16];
        char level[8];
        char message[280];
    } op_log_t;
    
    op_log_t op_log[MAX_LOG_BATCH_SIZE];
    
    if (!nvs_supabase_is_configured()) {
        return 0;
    }
    
    char url[256];
    nvs_get_supabase_log_url(url, sizeof(url));

    const char *api_key = nvs_get_supabase_api_key();
    if (!api_key || strlen(api_key) == 0) {
        return 0;
    }
    
    int uploaded = 0;
    uint32_t total_read_bytes = 0;
    const char *sys_name = nvs_get_system_name();
    
    // Get total line count from log system
    uint32_t total_lines = log_rotate_get_line_count();
    if (total_lines == 0) {
        WQMS_LOG_V("No logs in circular buffer");
        return 0;
    }
    
    WQMS_LOG_D("Total lines in log: %lu", total_lines);
    
    while (uploaded < total_lines) {
        WQMS_LOG_D("Uploaded count: %d", uploaded);
        // Read logs line by line using line-based API
        uint32_t read_offset = LOG_METADATA_SIZE + total_read_bytes; //second batch nasıl alınacak
        char line_buffer[LOG_MAX_LINE_LENGTH + 1];
        size_t bytes_read;
        int collected = 0;
        while (collected < MAX_LOG_BATCH_SIZE) {
            bytes_read = log_rotate_read_line(line_buffer, sizeof(line_buffer) - 1, &read_offset);
            total_read_bytes += bytes_read;
            if (bytes_read == 0) break;
            line_buffer[bytes_read] = '\0';
            
            // Parse the log line
            // Format: [YYYY-MM-DD HH:MM:SS] [MOD-LVL] Message
            uint64_t log_ts = 0;
            char timestamp_str[32] = "";
            char module[16] = "";
            char level[8] = "";
            char message[280] = "";
            
            char *p = line_buffer;
            
            // Parse timestamp: [YYYY-MM-DD HH:MM:SS]
            if (*p == '[') {
                p++;
                char *end = strchr(p, ']');
                if (end) {
                    size_t len = end - p;
                    if (len < sizeof(timestamp_str)) {
                        strncpy(timestamp_str, p, len);
                        timestamp_str[len] = '\0';
                        // Convert to timestamp
                        struct tm tm = {0};
                        char *result = strptime(timestamp_str, "%Y-%m-%d %H:%M:%S", &tm);
                        if (result) {
                            log_ts = mktime(&tm);
                        }
                    }
                    p = end + 1;
                }
            }
            
            // Skip if this log is older than last upload
            if (log_ts > 0 && log_ts <= last_log_upload_ts) {
                continue;
            }
            
            // Parse module and level: [MOD-LVL]
            while (*p == ' ') p++;
            if (*p == '[') {
                p++;
                char *end = strchr(p, '-');
                if (end) {
                    size_t len = end - p;
                    if (len < sizeof(module)) {
                        strncpy(module, p, len);
                        module[len] = '\0';
                    }
                    p = end + 1;
                }
                end = strchr(p, ']');
                if (end) {
                    size_t len = end - p;
                    if (len < sizeof(level)) {
                        strncpy(level, p, len);
                        level[len] = '\0';
                    }
                    p = end + 1;
                }
            }
            
            // Get message
            while (*p == ' ') p++;
            strncpy(message, p, sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
            
            if (strlen(message) > 0) {
                op_log[collected].log_ts = log_ts;
                strncpy(op_log[collected].timestamp_str, timestamp_str, sizeof(op_log[collected].timestamp_str) - 1);
                op_log[collected].timestamp_str[sizeof(op_log[collected].timestamp_str) - 1] = '\0';
                
                strncpy(op_log[collected].module, module, sizeof(op_log[collected].module) - 1);
                op_log[collected].module[sizeof(op_log[collected].module) - 1] = '\0';
                
                strncpy(op_log[collected].level, level, sizeof(op_log[collected].level) - 1);
                op_log[collected].level[sizeof(op_log[collected].level) - 1] = '\0';
                
                strncpy(op_log[collected].message, message, sizeof(op_log[collected].message) - 1);
                op_log[collected].message[sizeof(op_log[collected].message) - 1] = '\0';
                
                collected++;
            } else {
                WQMS_LOG_D("Message is corrupt -%s-", message);
            }
        }
        
        // Upload collected logs
        if (collected > 0) {
            WQMS_LOG_D("Collected count: %d", collected);
            cJSON *root = cJSON_CreateArray();
            if (!root) {
                WQMS_LOG_E("Failed to create JSON array for logs");
                return 0;
            }
            
            for (int i = 0; i < collected; i++) {
                cJSON *entry = cJSON_CreateObject();
                if (!entry) continue;
                
                cJSON_AddStringToObject(entry, "system", sys_name ? sys_name : "WQMS-System");
                cJSON_AddStringToObject(entry, "timestamp", op_log[i].timestamp_str);
                cJSON_AddStringToObject(entry, "module", op_log[i].module);
                cJSON_AddStringToObject(entry, "level", op_log[i].level);
                cJSON_AddStringToObject(entry, "message", op_log[i].message);
                cJSON_AddItemToArray(root, entry);
            }
            
            char *json_str = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            
            if (json_str) {
                esp_err_t err = http_post_json(url, api_key, json_str);
                
                if (err == ESP_OK) {
                    // Update last upload timestamp to the newest log we uploaded
                    for (int i = 0; i < collected; i++) {
                        if (op_log[i].log_ts > last_log_upload_ts) {
                            last_log_upload_ts = op_log[i].log_ts;
                        }
                    }
                    uploaded += collected;
                    WQMS_LOG_I("Uploaded %d logs from circular buffer", collected);
                } else {
                    WQMS_LOG_W("Failed to upload logs, will retry later");
                    // Log the first part of the JSON for debugging
                    WQMS_LOG_D("%.250s...", json_str);
/*                    if (strlen(json_str) > 250) {
                        int j = 250;
                        WQMS_LOG_D("==0==");
                        for (int i = 0; i < strlen(json_str); i=+j) {
                            WQMS_LOG_D("%.*s", j, &json_str[i]);
                        }
                        WQMS_LOG_D("==0==");
                    }
*/
                }
                free(json_str);
            }
        }
    }
    last_log_upload_time = esp_timer_get_time() / 1000000ULL;
    last_log_upload_ok = true;
    return uploaded;
}

// ============================================================
// Upload Task - Runs periodically
// ============================================================
static void upload_task(void *pvParameters) {
    WQMS_LOG_I("Supabase upload task initiated");
    
    uint32_t interval_sec = nvs_get_supabase_upload_interval();
    if (interval_sec < 10) interval_sec = 10;
    
    TickType_t interval_ticks = pdMS_TO_TICKS(interval_sec * 1000);
    
    while (is_running) {
        if (wifi_is_connected() && nvs_supabase_is_configured()) {
            xSemaphoreTake(upload_mutex, portMAX_DELAY);
            // Upload pending sensors (highest priority)
            int sensor_uploaded = upload_pending_sensors();
            // Upload pending logs
            int log_uploaded = 0;
//            log_uploaded = upload_pending_logs(); //oko
            if (sensor_uploaded > 0 || log_uploaded > 0) {
                WQMS_LOG_I("Uploaded %d sensors, %d logs", sensor_uploaded, log_uploaded);
            }
            xSemaphoreGive(upload_mutex);
        }
        vTaskDelay(interval_ticks);
    }
    WQMS_LOG_I("Supabase upload task ended");
    vTaskDelete(NULL);
}

// ============================================================
// Public Functions
// ============================================================
esp_err_t supabase_upload_init(void) {
    if (is_initialized) {
        return ESP_OK;
    }
    
    upload_mutex = xSemaphoreCreateMutex();
    if (!upload_mutex) {
        WQMS_LOG_E("Failed to create upload mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize timestamps from NVS (optional - could store last upload times)
    last_sensor_upload_ts = 0;
    last_log_upload_ts = 0;
    
    // Log certificate status
    if (!cert_manager_has(CERT_TYPE_SUPABASE)) {
        WQMS_LOG_E("No Supabase TLS certificate in NVS, aborting Supabase upload.");
        return ESP_ERR_INVALID_STATE;
    }

    is_initialized = true;
    WQMS_LOG_I("Supabase upload system initialized");
    return ESP_OK;
}

esp_err_t supabase_upload_start(void) {
    if (!is_initialized) {
        WQMS_LOG_E("Supabase upload not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (is_running) {
        return ESP_OK;
    }
    
    is_running = true;
    
    BaseType_t result = xTaskCreate(
        upload_task,
        "supabase_up",
        STACK_SIZE_SUPABASE_UPLOAD,
        NULL,
        PRIORITY_UPLOAD_TASK,
        &upload_task_handle
    );
    
    if (result != pdPASS) {
        is_running = false;
        WQMS_LOG_E("Failed to create upload task");
        return ESP_ERR_NO_MEM;
    }
    
    WQMS_LOG_I("Supabase upload task started");
    return ESP_OK;
}

esp_err_t supabase_upload_stop(void) {
    if (!is_running) {
        return ESP_OK;
    }
    
    is_running = false;
    
    if (upload_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelete(upload_task_handle);
        upload_task_handle = NULL;
    }
    
    WQMS_LOG_I("Supabase upload stopped");
    return ESP_OK;
}

void supabase_get_status(bool *sensor_ok, bool *log_ok) {
    if (sensor_ok) *sensor_ok = last_sensor_upload_ok;
    if (log_ok) *log_ok = last_log_upload_ok;
}

uint64_t supabase_get_last_sensor_upload(void) {
    return last_sensor_upload_time;
}

uint64_t supabase_get_last_log_upload(void) {
    return last_log_upload_time;
}

// ============================================================
// Sensor Configuration Sync
// ============================================================

/**
 * @brief Sync current sensor configuration to Supabase
 * This sends the current sensor names/units to the sensor_config table
 */
esp_err_t supabase_sync_sensor_config(void) {
    if (!nvs_supabase_is_configured()) {
        WQMS_LOG_W("Supabase not configured, cannot sync sensor config");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check WiFi
    if (!wifi_is_connected()) {
        WQMS_LOG_W("WiFi not connected, cannot sync sensor config");
        return ESP_ERR_INVALID_STATE;
    }
    
    const char *api_key = nvs_get_supabase_api_key();
    char sensor_url[256];
    nvs_get_supabase_sensor_url(sensor_url, sizeof(sensor_url));
    
    if (!api_key || strlen(api_key) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char config_url[256];
    nvs_get_supabase_sensorconfig_url(config_url, sizeof(config_url));

    const char *sys_name = nvs_get_system_name();
    if (!sys_name) sys_name = "WQMS-System";
    
    // Build JSON payload with all sensors
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        
        cJSON_AddNumberToObject(entry, "id", i);
        cJSON_AddStringToObject(entry, "system", sys_name);
        
        const char *name = sensor_get_name(i);
        cJSON_AddStringToObject(entry, "sensor_name", name ? name : "Unknown");
        
        // Get sensor unit (you may want to store this in NVS as well)
        const char *unit = sensor_get_unit(i);
        cJSON_AddStringToObject(entry, "unit", unit ? unit : "Unknown");

        cJSON_AddItemToArray(root, entry);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    // Send HTTP POST to sync config
    esp_err_t err = http_post_json(config_url, api_key, json_str);
    free(json_str);
    
    if (err == ESP_OK) {
        WQMS_LOG_I("Sensor configuration synced to Supabase");
    } else {
        WQMS_LOG_E("Failed to sync sensor configuration: %s", esp_err_to_name(err));
    }
    
    return err;
}