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
#include "esp_timer.h"        // FIX: needed for idle-wait timing
#include "nvs.h"

#include "wifi_manager.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "watchdog.h"
#include "nvs_config.h"

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

// FIX: set while a user-initiated wifi_connect_sta() owns the STA flow.
// The WIFI_EVENT_STA_DISCONNECTED handler must not auto-reconnect while this
// is true, otherwise it races wifi_connect_sta() and puts the driver back
// into "sta is connecting" state, which makes set_config fail with 0x3006.
static volatile bool sta_manual_connect_in_progress = false;

// ============================================================
// Internal Functions
// ============================================================

// FIX: explicit save - does not read back from the driver (safer while
// credentials are being changed), writes exactly what the caller supplies.
static void wqms_save_wifi_creds_explicit(const char *ssid, const char *password) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "ssid", ssid ? ssid : "");
        nvs_set_str(handle, "password", password ? password : "");
        nvs_commit(handle);
        nvs_close(handle);
        WQMS_LOG_I("WiFi credentials saved to NVS");
    }
}

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
    // FIX: guarantee a sane default even if nvs_open fails
    if (ssid && max_len > 0) ssid[0] = '\0';
    if (password && max_len > 0) password[0] = '\0';

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

// FIX: bounded wait that returns once the STA is NOT in "connecting" state.
// Uses esp_wifi_get_config(WIFI_IF_STA) as a proxy: it returns
// ESP_ERR_WIFI_STATE while connecting and ESP_OK when idle.
static bool wifi_wait_sta_idle(uint32_t timeout_ms) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return true;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) return true;

    int64_t start = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000) - start < (int64_t)timeout_ms) {
        wifi_config_t probe;
        if (esp_wifi_get_config(WIFI_IF_STA, &probe) == ESP_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    WQMS_LOG_W("STA did not become idle within %ums", (unsigned)timeout_ms);
    return false;
}

// FIX: stop any in-flight STA attempt and wait until the driver is ready
// to accept a new configuration.
static void wifi_stop_sta_connection(void) {
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT &&
        err != ESP_ERR_WIFI_NOT_STARTED) {
        WQMS_LOG_W("esp_wifi_disconnect: %s", esp_err_to_name(err));
    }
    wifi_wait_sta_idle(500);
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

    // FIX: stop any in-flight STA attempt BEFORE touching the mode/config.
    // Otherwise set_config(WIFI_IF_STA) can fail with ESP_ERR_WIFI_STATE.
    sta_manual_connect_in_progress = false;
    wifi_stop_sta_connection();

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
        // FIX: runtime call - don't abort on failure, log and continue
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            WQMS_LOG_W("set_config(STA) in AP+STA: %s", esp_err_to_name(err));
        }
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    
    wifi_mode = 2;
    wifi_retry_count = 0;   // FIX: fresh retry budget
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
                // FIX: don't auto-connect if a manual connect owns the flow
                if (!sta_manual_connect_in_progress &&
                    (wifi_mode == 1 || wifi_mode == 2)) {
                    esp_wifi_connect();
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                WQMS_LOG_I("WiFi STA connected");
                wifi_retry_count = 0;
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                // FIX: keep reason for debugging
                wifi_event_sta_disconnected_t *disc =
                    (wifi_event_sta_disconnected_t *)event_data;
                WQMS_LOG_I("WiFi STA disconnected (reason=%d)",
                           disc ? disc->reason : -1);
                wifi_connected = 0;

                // FIX: hand control back to wifi_connect_sta(); do NOT
                // call esp_wifi_connect() with the old config here.
                if (sta_manual_connect_in_progress) {
                    break;
                }

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
            }
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        WQMS_LOG_I("Got IP: %s", wifi_ip);
        wifi_connected = 1;
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        // NOTE: credentials are now saved explicitly by wifi_connect_sta()
        // with exactly what the user submitted. Reading back from the
        // driver here can race the next manual connect.
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

        // FIX: clear stale bits before waiting (first boot usually ok, but
        // this is correct after a soft reset / panic reboot).
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
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
            // FIX: tell the handler we're heading into AP+STA before we call
            // wqms_start_ap_sta_mode(), so STA_START behaves consistently.
            wifi_mode = 2;
            wqms_start_ap_sta_mode();
            WQMS_LOG_I("Switched to AP+STA mode");
        }
    } else {
        WQMS_LOG_I("No STA credentials found, starting AP+STA mode");
        wifi_mode = 2;
        wqms_start_ap_sta_mode();
        WQMS_LOG_I("Switched to AP+STA mode");
    }
    
    int8_t tx_power_value = WIFI_TX_POWER_DBM * 4;  // Convert to 0.25dBm units → 40
    esp_err_t err = esp_wifi_set_max_tx_power(tx_power_value);
    if (err == ESP_OK) {
        WQMS_LOG_I("TX power set to %d dBm (value: %d)", WIFI_TX_POWER_DBM, tx_power_value);
    } else {
        WQMS_LOG_E("Failed to set TX power: %s", esp_err_to_name(err));
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
        // FIX: don't auto-reconnect from the handler during the switch
        sta_manual_connect_in_progress = false;
        wifi_stop_sta_connection();
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

        // FIX: stop in-flight attempt before set_config
        sta_manual_connect_in_progress = false;
        wifi_stop_sta_connection();

        wifi_config_t sta_config = {0};
        memcpy(sta_config.sta.ssid, ssid, strlen(ssid));
        memcpy(sta_config.sta.password, password, strlen(password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        // FIX: runtime call - no ESP_ERROR_CHECK
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            WQMS_LOG_E("set_config failed: %s", esp_err_to_name(err));
            return -3;
        }
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();   // FIX: no ESP_ERROR_CHECK here either
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
    if (!ssid) return -1;
    if (!password) password = "";   // FIX: allow open networks
    if (strlen(ssid) == 0) return -1;

    WQMS_LOG_I("Connecting to STA: %s", ssid);

    // FIX: sanity check
    wifi_mode_t cur_mode;
    if (esp_wifi_get_mode(&cur_mode) == ESP_ERR_WIFI_NOT_INIT) {
        WQMS_LOG_E("WiFi not initialized");
        return -2;
    }

    // ------------------------------------------------------------------
    // FIX (the crash):
    //   Do NOT call wifi_set_mode(2) here. It calls wqms_start_ap_sta_mode(),
    //   which calls esp_wifi_connect() with the OLD credentials - leaving the
    //   driver in "sta is connecting". The subsequent
    //   esp_wifi_set_config() with the NEW credentials then fails with
    //   ESP_ERR_WIFI_STATE (0x3006) and ESP_ERROR_CHECK aborts.
    //
    //   Instead: switch mode ourselves (without starting any connect), then
    //   stop the STA, wait for it to become idle, and only then write config.
    // ------------------------------------------------------------------
    if (cur_mode != WIFI_MODE_APSTA) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            WQMS_LOG_E("set_mode(APSTA) failed: %s", esp_err_to_name(err));
            return -3;
        }

        // Re-apply AP config so the dashboard stays reachable.
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(wifi_ap_ssid, sizeof(wifi_ap_ssid),
                 "%s%02X%02X%02X%02X%02X%02X",
                 WIFI_AP_SSID_PREFIX,
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        wifi_config_t ap_config = {0};
        ap_config.ap.ssid_len = strlen(wifi_ap_ssid);
        ap_config.ap.channel = 1;
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
        ap_config.ap.max_connection = 4;
        memcpy(ap_config.ap.ssid, wifi_ap_ssid, strlen(wifi_ap_ssid));
        memcpy(ap_config.ap.password, WIFI_AP_PASSWORD, strlen(WIFI_AP_PASSWORD));

        err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        if (err != ESP_OK) {
            WQMS_LOG_W("set_config(AP) failed: %s", esp_err_to_name(err));
        }
    }
    wifi_mode = 2;

    // Take ownership of the STA connection flow.
    sta_manual_connect_in_progress = true;
    wifi_retry_count = 0;
    wifi_connected = 0;

    // FIX: disconnect FIRST, then wait for the driver to leave "connecting".
    wifi_stop_sta_connection();

    // FIX: clear stale event bits from any previous attempt.
    xEventGroupClearBits(wifi_event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // FIX: now it is safe to write the new credentials.
    wifi_config_t sta_config = {0};
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(password);
    if (ssid_len > sizeof(sta_config.sta.ssid))     ssid_len = sizeof(sta_config.sta.ssid);
    if (pass_len > sizeof(sta_config.sta.password)) pass_len = sizeof(sta_config.sta.password);
    memcpy(sta_config.sta.ssid, ssid, ssid_len);
    memcpy(sta_config.sta.password, password, pass_len);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        WQMS_LOG_E("set_config(STA) failed: %s", esp_err_to_name(err));
        sta_manual_connect_in_progress = false;
        return -4;
    }

    // FIX: now connect (no ESP_ERROR_CHECK; on failure we cleanly release).
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        WQMS_LOG_E("esp_wifi_connect failed: %s", esp_err_to_name(err));
        sta_manual_connect_in_progress = false;
        return -5;
    }

    // Wait for result.
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));

    sta_manual_connect_in_progress = false;

    if (bits & WIFI_CONNECTED_BIT) {
        WQMS_LOG_I("STA connected: %s", ssid);
        snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
        // Actively switch to STA-only mode and turn off the AP
        esp_wifi_set_mode(WIFI_MODE_STA);
        wifi_mode = 1;  // update state
        wqms_save_wifi_creds_explicit(ssid, password);
        return 0;
    }

    WQMS_LOG_E("STA connection failed: %s", ssid);
    wifi_mode = 2;                                  // keep dashboard reachable
    wifi_retry_count = WIFI_MAX_RETRIES;            // stop the auto-retry storm
    return -1;
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

    // FIX: release manual-connect ownership before switching mode
    sta_manual_connect_in_progress = false;
    wifi_disconnect_sta();

    wifi_mode_t current_mode;
    esp_wifi_get_mode(&current_mode);
    if (current_mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    wifi_mode = 2;   // FIX: keep state variable in sync
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