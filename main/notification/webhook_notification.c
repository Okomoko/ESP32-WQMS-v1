// webhook_notification.c
// IFTTT Webhook-based notification implementation

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "webhook_notification.h"
#include "wifi_manager.h"
#include "ntp_client.h"
#include "nvs_config.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

static webhook_config_t webhook_config = {0};
static int webhook_initialized = 0;

static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

// ============================================================
// Internal: Build JSON Payload
// ============================================================
static char* build_webhook_payload(wh_notif_type_t type, const char *event, const char *message) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "system_name", nvs_get_system_name());
    cJSON_AddStringToObject(root, "mac", wifi_get_mac());
    cJSON_AddStringToObject(root, "ip", wifi_get_ip());
    cJSON_AddStringToObject(root, "wifi_ssid", wifi_get_ssid());
    cJSON_AddStringToObject(root, "wifi_mode", wifi_get_mode_string());
    
    const char *type_str[] = {"INFO", "WARNING", "ALERT", "CRITICAL"};
    cJSON_AddStringToObject(root, "event_type", type_str[type]);
    cJSON_AddStringToObject(root, "event", event ? event : "Unknown");
    cJSON_AddStringToObject(root, "message", message ? message : "");
    
    cJSON_AddStringToObject(root, "email", webhook_config.emails);
    cJSON_AddStringToObject(root, "subject", webhook_config.subject);
    
    time_t now = time(NULL);
    char ts[32];
    struct tm *tm_info = localtime(&now);
    if (tm_info) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        cJSON_AddStringToObject(root, "timestamp", ts);
        cJSON_AddNumberToObject(root, "epoch", (uint32_t)now);
    } else {
        cJSON_AddStringToObject(root, "timestamp", "N/A");
        cJSON_AddNumberToObject(root, "epoch", 0);
    }
    
    cJSON_AddNumberToObject(root, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_seconds", esp_timer_get_time() / 1000000ULL);
    cJSON_AddNumberToObject(root, "ntp_synced", ntp_is_synced());
    cJSON_AddStringToObject(root, "version", "v1.0.0");
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    return json_str;
}

// ============================================================
// Internal: Substitute {{system_name}} in subject
// ============================================================
static void substitute_subject(char *dest, const char *src, size_t max_len) {
    const char *pattern = "{{system_name}}";
    char *found = strstr(src, pattern);
    
    if (found) {
        size_t prefix_len = found - src;
        strncpy(dest, src, prefix_len);
        dest[prefix_len] = '\0';
        strncat(dest, nvs_get_system_name(), max_len - strlen(dest) - 1);
        const char *suffix = found + strlen(pattern);
        strncat(dest, suffix, max_len - strlen(dest) - 1);
    } else {
        strncpy(dest, src, max_len - 1);
        dest[max_len - 1] = '\0';
    }
}

// ============================================================
// Internal: URL ENCODE
// ============================================================

static char* url_encode(const char* str) {
    if (str == NULL) return NULL;
    
    size_t len = strlen(str);
    // Worst case: every character needs 3 bytes (%XX)
    char* encoded = malloc(len * 3 + 1);
    if (encoded == NULL) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        
        // Safe characters for form data
        if ((c >= 'A' && c <= 'Z') || 
            (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || 
            c == '-' || c == '.' || c == '_' || c == '~' ||
            c == '/') {  // Keep '/' as is for email addresses
            encoded[j++] = c;
        } else if (c == ' ') {
            encoded[j++] = '+';  // Space becomes '+'
        } else {
            // Encode everything else
            j += snprintf(&encoded[j], 4, "%%%02X", c);
        }
    }
    
    encoded[j] = '\0';
    return encoded;
}

// ============================================================
// Internal: BASE64 ENCODE
// ============================================================

static char* base64_encode(const char* data) {
	size_t input_length = strlen(data);
    size_t output_length = 4 * ((input_length + 2) / 3);
    char* encoded_data = malloc(output_length + 1);
    if (encoded_data == NULL) {
        return NULL;
    }
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    // Add padding
    size_t mod = input_length % 3;
    if (mod == 1) {
        encoded_data[output_length - 2] = '=';
        encoded_data[output_length - 1] = '=';
    } else if (mod == 2) {
        encoded_data[output_length - 1] = '=';
    }
    
    encoded_data[output_length] = '\0';
    return encoded_data;
}

// ============================================================
// Internal: HTTP POST Task (HTTP only - no TLS)
// ============================================================
typedef struct {
    char payload[1024];
    char subject[128];
    char emails[256];
} webhook_task_data_t;

static void webhook_send_task(void *pvParameters) {
    webhook_task_data_t *data = (webhook_task_data_t*)pvParameters;
    if (!data) {
        vTaskDelete(NULL);
        return;
    }

//    WQMS_LOG_I("Subject: %s, Recipients: %s", data->subject, data->emails);

//    extern const char root_cert_pem_start[] asm("_binary_root_cert_pem_start");
//    extern const char root_cert_pem_end[] asm("_binary_root_cert_pem_end");
    // Get API key from environment variable or use default
    const char* api_key = getenv("API_KEY");
    if (api_key == NULL) {
        api_key = DEFAULT_API_KEY;
    }

    // Build URL
    char url[256];
    snprintf(url, sizeof(url), "https://api.mailgun.net/v3/%s/messages", DEFAULT_DOMAIN);

    // URL encode fields
    char* from_encoded = url_encode(DEFAULT_FROM);
    char* to_encoded = url_encode(data->emails);
    char* subject_encoded = url_encode(data->subject);
    char* text_encoded = url_encode(data->payload);

    // Build POST data
    char post_data[2048];
    snprintf(post_data, sizeof(post_data), "from=%s&to=%s&subject=%s&text=%s", from_encoded, to_encoded, subject_encoded, text_encoded);

    free(from_encoded);
    free(to_encoded);
    free(subject_encoded);
    free(text_encoded);

    // Setup HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .keep_alive_enable = false,
//        .cert_pem = root_cert_pem_start,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        WQMS_LOG_E("Failed to initialize HTTP client");
        vTaskDelete(NULL);
        return;
    }
    
    // Set basic authentication with base64 encoding
    char credentials[256];
    snprintf(credentials, sizeof(credentials), "api:%s", api_key);
    char* encoded_creds = base64_encode(credentials);
    if (encoded_creds) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Basic %s", encoded_creds);
        esp_http_client_set_header(client, "Authorization", auth_header);
        free(encoded_creds);
    }
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Accept", "application/json");
    
    // Set POST data
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        WQMS_LOG_I("HTTP Status = %d", status);
        
        if (status >= 200 && status < 300) {
            // Read and parse response
            char* response = malloc(2048);
            if (response) {
                int total_read = 0;
                int data_read;
                while ((data_read = esp_http_client_read(client, response + total_read, 1024)) > 0) {
                    total_read += data_read;
                    if (total_read >= 2047) break;
                }
                response[total_read] = '\0';
                
                // Parse JSON
                cJSON* json = cJSON_Parse(response);
                if (json) {
                    char* json_str = cJSON_Print(json);
                    WQMS_LOG_I("Response JSON: %s", json_str);
                    free(json_str);
                    cJSON_Delete(json);
                } else {
                    WQMS_LOG_I("Response: %s", response);
                }
                free(response);
            }
            WQMS_LOG_I("Email sent successfully!");
        } else {
            WQMS_LOG_E("Email sending failed with status: %d", status);
            char buffer[512];
            int data_read = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
            if (data_read > 0) {
                buffer[data_read] = '\0';
                WQMS_LOG_E("Error response: %s", buffer);
            }
        }
    } else {
        WQMS_LOG_E("HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(data);
    vTaskDelete(NULL);
    return;
}

// ============================================================
// Public Functions
// ============================================================

void webhook_notification_init(void) {
//    WQMS_LOG_I("webhook_notification_init() called");
    
    webhook_config_t config = {0};
    webhook_load_config(&config);
    
//    WQMS_LOG_I("Loaded from NVS - URL: '%s', Recipients: '%s'", config.url, config.emails);
    
    if (strlen(config.emails) == 0) {
        strncpy(config.emails, DEFAULT_RECIPIENTS, sizeof(config.emails) - 1);
        config.emails[sizeof(config.emails) - 1] = '\0';
        WQMS_LOG_I("Recipients was empty, set to default: '%s'", config.emails);
    }
    if (strlen(config.subject) == 0) {
        strncpy(config.subject, DEFAULT_SUBJECT, sizeof(config.subject) - 1);
        config.subject[sizeof(config.subject) - 1] = '\0';
        WQMS_LOG_I("Subject was empty, set to default: '%s'", config.subject);
    }
    config.enabled = 1;
    
    webhook_save_config(&config);
    
    webhook_initialized = 1;
}

void webhook_load_config(webhook_config_t *config) {
    if (!config) {
        WQMS_LOG_W("Webhook config is not loaded: ", config);
        return;
    }

    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t len = sizeof(config->emails);
    esp_err_t err = nvs_get_str(handle, "webhook_emails", config->emails, &len);
    if (err != ESP_OK) {
        WQMS_LOG_W("Webhook recepients error : ", err);
        config->emails[0] = '\0';
    }
    
    len = sizeof(config->subject);
    err = nvs_get_str(handle, "webhook_subject", config->subject, &len);
    if (err != ESP_OK) {
        WQMS_LOG_W("Webhook subject error : ", err);
        config->subject[0] = '\0';
    }
    
    uint8_t enabled = 1;
    err = nvs_get_u8(handle, "webhook_enabled", &enabled);
    if (err != ESP_OK) {
        enabled = 1;
    }
    config->enabled = enabled;
    
    nvs_close(handle);
    
    WQMS_LOG_W("Webhook config loaded: recipients=%s, subject=%s, enabled=%d", config->emails, config->subject, config->enabled);
}

void webhook_save_config(webhook_config_t *config) {
    if (!config) return;

    config->emails[sizeof(config->emails) - 1] = '\0';
    config->subject[sizeof(config->subject) - 1] = '\0';

//    WQMS_LOG_W("Webhook config will be saved: recipients=%s, subject=%s, enabled=%d", config->emails, config->subject, config->enabled);

    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READWRITE, &handle) != ESP_OK) {
        WQMS_LOG_E("Failed to open NVS for webhook config");
        return;
    }

    nvs_set_str(handle, "webhook_emails", config->emails);
    nvs_set_str(handle, "webhook_subject", config->subject);
    nvs_set_u8(handle, "webhook_enabled", config->enabled);
    nvs_commit(handle);
    nvs_close(handle);
    
    memcpy(&webhook_config, config, sizeof(webhook_config_t));

    WQMS_LOG_I("Webhook config saved to NVS");
}

int webhook_send_notification(wh_notif_type_t type, const char *event, const char *message) {
    if (!webhook_initialized || !webhook_config.enabled) {
        WQMS_LOG_W("Webhook notifications disabled");
        return -1;
    }

    char *json_payload = build_webhook_payload(type, event, message);
    if (!json_payload) {
        WQMS_LOG_E("Failed to build webhook payload");
        return -3;
    }

    webhook_task_data_t *data = malloc(sizeof(webhook_task_data_t));
    if (!data) {
        free(json_payload);
        WQMS_LOG_E("Failed to allocate webhook task data");
        return -4;
    }
    
    strncpy(data->payload, json_payload, sizeof(data->payload) - 1);
    data->payload[sizeof(data->payload) - 1] = '\0';
    free(json_payload);
    
    substitute_subject(data->subject, webhook_config.subject, sizeof(data->subject));
    strncpy(data->emails, webhook_config.emails, sizeof(data->emails) - 1);
    data->emails[sizeof(data->emails) - 1] = '\0';
    
    if (xTaskCreate(webhook_send_task, "webhook_send", WEBHOOK_TASK_STACK_SIZE, data, WEBHOOK_TASK_PRIORITY, NULL) != pdPASS) {
        WQMS_LOG_E("Failed to create webhook task");
        free(data);
        return -5;
    }
    
    APP_LOG_I("Webhook notification queued: %s - %s", event ? event : "Unknown", message ? message : "");
    return 0;
}

int webhook_send_rule_alert(const char *rule_name, const char *rule_desc) {
    if (!rule_name) return -1;
    
    char event[64];
    char message[128];
    
    snprintf(event, sizeof(event), "Rule Triggered: %s", rule_name);
    snprintf(message, sizeof(message), "%s | System: %s", 
             rule_desc ? rule_desc : "No description", nvs_get_system_name());
    
    return webhook_send_notification(WH_TYPE_ALERT, event, message);
}

int webhook_test_notification(void) {
    return webhook_send_notification(WH_TYPE_INFO, "Test Notification", 
                                     "This is a test webhook notification from your WQMS system.");
}

int webhook_is_enabled(void) {
    return webhook_initialized && webhook_config.enabled;
}