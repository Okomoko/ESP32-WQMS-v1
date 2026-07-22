// wifi_manager.c
// WiFi manager implementation - STA mode with AP fallback and AP+STA transition

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs.h"

#include "wifi_manager.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "watchdog.h"
#include "nvs_config.h"

// ============================================================
// Constants
// ============================================================
#define WIFI_AP_SSID_PREFIX "WQMS-"
#define WIFI_AP_PASSWORD "WQMSwqms"
#define WIFI_MAX_RETRIES 5
#define WIFI_RETRY_DELAY_MS 2000
#define WIFI_SCAN_MAX_NETWORKS 20

// ============================================================
// Event Group Bits
// ============================================================
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// ============================================================
// Static Variables
// ============================================================
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count = 0;
static char wifi_ssid[33] = {0};
static char wifi_ip[16] = "0.0.0.0";
static char wifi_mac[18] = {0};
static int wifi_mode = 0;  // 0=AP, 1=STA, 2=AP+STA
static int wifi_connected = 0;
static char wifi_ap_ssid[33] = {0};
static wifi_ap_record_t *scan_results = NULL;
static int scan_result_count = 0;

// ============================================================
// Internal Functions
// ============================================================

static void wqms_save_wifi_creds(void) {
    wifi_config_t sta_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_config) == ESP_OK) {
        nvs_handle_t handle;
        if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_str(handle, "ssid", (char*)sta_config.sta.ssid);
            nvs_set_str(handle, "password", (char*)sta_config.sta.password);
            nvs_commit(handle);
            nvs_close(handle);
            WQMS_LOG_I("WiFi credentials saved to NVS");
        }
    }
}

static void wqms_load_wifi_creds(char *ssid, char *password, size_t max_len) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = max_len;
        if (nvs_get_str(handle, "ssid", ssid, &len) != ESP_OK) {
            ssid[0] = '\0';
        }
        len = max_len;
        if (nvs_get_str(handle, "password", password, &len) != ESP_OK) {
            password[0] = '\0';
        }
        nvs_close(handle);
    }
}

static void wqms_start_ap_mode(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid), "%s%02X%02X%02X%02X%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(wifi_ap_ssid),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    memcpy(ap_config.ap.ssid, wifi_ap_ssid, strlen(wifi_ap_ssid));
    memcpy(ap_config.ap.password, WIFI_AP_PASSWORD, strlen(WIFI_AP_PASSWORD));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    wifi_mode = 0;
    wifi_connected = 0;
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", wifi_ap_ssid);
    snprintf(wifi_ip, sizeof(wifi_ip), "192.168.4.1");
    WQMS_LOG_I("AP mode started: SSID=%s, Password=%s", wifi_ap_ssid, WIFI_AP_PASSWORD);
}

static void wqms_start_ap_sta_mode(void) {
    // Get MAC for AP SSID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid), "%s%02X%02X%02X%02X%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(wifi_ap_ssid),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    memcpy(ap_config.ap.ssid, wifi_ap_ssid, strlen(wifi_ap_ssid));
    memcpy(ap_config.ap.password, WIFI_AP_PASSWORD, strlen(WIFI_AP_PASSWORD));
    
    // Load STA credentials
    char ssid[33] = {0};
    char password[64] = {0};
    wqms_load_wifi_creds(ssid, password, sizeof(ssid));
    
    // Configure STA
    wifi_config_t sta_config = {0};
    if (strlen(ssid) > 0) {
        memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
        memcpy(sta_config.sta.password, password, strlen(password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    
    // Set AP+STA mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    if (strlen(ssid) > 0) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    
    wifi_mode = 2;
//    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", wifi_ap_ssid);
    WQMS_LOG_I("AP+STA mode started: AP SSID=%s", wifi_ap_ssid);
    
    // If STA credentials exist, attempt connection
    if (strlen(ssid) > 0) {
        esp_wifi_connect();
    }
}

// ============================================================
// Event Handler
// ============================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                WQMS_LOG_I("WiFi STA started");
                if (wifi_mode == 2) {
                    esp_wifi_connect();
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                WQMS_LOG_I("WiFi STA connected");
                wifi_retry_count = 0;
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                WQMS_LOG_I("WiFi STA disconnected");
                if (wifi_retry_count < WIFI_MAX_RETRIES) {
                    wifi_retry_count++;
                    WQMS_LOG_W("WiFi retry %d/%d", wifi_retry_count, WIFI_MAX_RETRIES);
                    vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
                    if (wifi_mode == 1 || wifi_mode == 2) {
                        esp_wifi_connect();
                    }
                } else {
                    WQMS_LOG_E("WiFi connection failed");
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        WQMS_LOG_I("Got IP: %s", wifi_ip);
        wifi_connected = 1;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        wqms_save_wifi_creds();
    }
}

// ============================================================
// Public Functions
// ============================================================

void wifi_init(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(wifi_mac, sizeof(wifi_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // Check if STA credentials exist
    char ssid[33] = {0};
    char password[64] = {0};
    wqms_load_wifi_creds(ssid, password, sizeof(ssid));
    
    if (strlen(ssid) > 0) {
        WQMS_LOG_I("Found STA credentials: %s", ssid);
        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
        memcpy(sta_config.sta.password, password, strlen(password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(30000));
        if (bits & WIFI_CONNECTED_BIT) {
            WQMS_LOG_I("STA connected successfully");
            snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
            wifi_mode = 1;
        } else {
            WQMS_LOG_W("STA connection failed, starting to AP+STA mode");
            wqms_start_ap_sta_mode();
            WQMS_LOG_I("Switched to AP+STA mode");
        }
    } else {
        WQMS_LOG_I("No STA credentials found, starting AP+STA mode");
        wqms_start_ap_sta_mode();
        WQMS_LOG_I("Switched to AP+STA mode");
    }
    
    watchdog_register_module(WDT_MODULE_WIFI, 30);
}

int wifi_get_mode(void) {
    return wifi_mode;
}

const char* wifi_get_ssid(void) {
    return wifi_ssid;
}

const char* wifi_get_ap_ssid(void) {
    return wifi_ap_ssid;
}

const char* wifi_get_ap_password(void) {
    return WIFI_AP_PASSWORD;
}

const char* wifi_get_ip(void) {
    return wifi_ip;
}

const char* wifi_get_mac(void) {
    return wifi_mac;
}

int wifi_set_mode(int mode) {
    if (mode < 0 || mode > 2) return -1;
    
    WQMS_LOG_I("Setting WiFi mode to %d (%s)", mode, 
               mode == 0 ? "AP" : mode == 1 ? "STA" : "AP+STA");
    
    if (mode == 0) {
        // AP only
        wqms_start_ap_mode();
        wifi_mode = 0;
    } else if (mode == 1) {
        // STA only
        char ssid[33] = {0};
        char password[64] = {0};
        wqms_load_wifi_creds(ssid, password, sizeof(ssid));
        if (strlen(ssid) == 0) {
            WQMS_LOG_E("No STA credentials configured");
            return -2;
        }
        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
        memcpy(sta_config.sta.password, password, strlen(password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
        wifi_mode = 1;
        WQMS_LOG_I("Switched to STA mode: %s", ssid);
    } else if (mode == 2) {
        // AP + STA
        wqms_start_ap_sta_mode();
        WQMS_LOG_I("Switched to AP+STA mode");
    }
    return 0;
}

const char* wifi_get_mode_string(void) {
    if (wifi_mode == 0) return "AP";
    if (wifi_mode == 1) return "STA";
    if (wifi_mode == 2) return "AP+STA";
    return "Unknown";
}

int wifi_connect_sta(const char *ssid, const char *password) {
    if (!ssid || !password) return -1;
    
    WQMS_LOG_I("Connecting to STA: %s", ssid);
    
    // Set to AP+STA mode to maintain connectivity
    wifi_set_mode(2);
    
    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_config.sta.password, password, strlen(password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        WQMS_LOG_I("STA connected: %s", ssid);
        snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
        wifi_mode = 1;
        wqms_save_wifi_creds();
        WQMS_LOG_I("WiFi credentials saved, network will restart");
        return 0;
    } else {
        WQMS_LOG_E("STA connection failed: %s", ssid);
        return -1;
    }
}

void wifi_disconnect_sta(void) {
    esp_wifi_disconnect();
    wifi_connected = 0;
    WQMS_LOG_I("STA disconnected");
}

void wifi_forget_network(void) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "ssid");
        nvs_erase_key(handle, "password");
        nvs_commit(handle);
        nvs_close(handle);
        WQMS_LOG_I("WiFi credentials erased.");
    }
    wifi_disconnect_sta();
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    WQMS_LOG_I("Switched to AP+STA mode");
}

int wifi_is_connected(void) {
    return wifi_connected;
}

int wifi_get_rssi(void) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        return info.rssi;
    }
    return 0;
}

int wifi_scan_networks(void) {
    if (scan_results) {
        free(scan_results);
        scan_results = NULL;
        scan_result_count = 0;
    }
    
    // Ensure we're in AP+STA mode for scanning
    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
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
    
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        WQMS_LOG_E("WiFi scan failed: %s", esp_err_to_name(err));
        return -1;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        scan_result_count = 0;
        return 0;
    }
    
    if (ap_count > WIFI_SCAN_MAX_NETWORKS) {
        ap_count = WIFI_SCAN_MAX_NETWORKS;
    }
    
    scan_results = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!scan_results) {
        WQMS_LOG_E("Failed to allocate memory for scan results");
        return -1;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, scan_results);
    scan_result_count = ap_count;
    
    WQMS_LOG_I("WiFi scan found %d networks", ap_count);
    return ap_count;
}

int wifi_get_scan_results(wifi_ap_record_t **records, int *count) {
    if (!records || !count) return -1;
    *records = scan_results;
    *count = scan_result_count;
    return 0;
}