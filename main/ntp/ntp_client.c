// ntp_client.c
// NTP client implementation - aggressive boot, 1-hour normal sync

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

#include "ntp_client.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "watchdog.h"

// ============================================================
// Static Variables
// ============================================================
static const char *default_servers[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
static char timezone_str[16] = NTP_DEFAULT_TIMEZONE;
static char server_list[256] = "pool.ntp.org,time.google.com,time.cloudflare.com";
static uint32_t manual_time = 0;
static uint8_t manual_time_set = 0;
static int ntp_synced = 0;
static int sync_attempts = 0;
static ntp_mode_t sync_mode = NTP_MODE_FAST;
static uint32_t backoff_sec = NTP_BACKOFF_BASE_SEC;
static TaskHandle_t ntp_task_handle = NULL;

// ============================================================
// Internal Functions
// ============================================================

static void apply_timezone(void) {
    setenv("TZ", timezone_str, 1);
    tzset();
    WQMS_LOG_D("Timezone set: %s", timezone_str);
}

static uint32_t get_time_from_ntp(void) {
    time_t now = time(NULL);
    if (now < 946684800) {
        return 0;
    }
    return (uint32_t)now;
}

// Load NTP servers from NVS
static void load_ntp_servers_from_nvs(void) {
    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(server_list);
        if (nvs_get_str(handle, "ntp_servers", server_list, &len) == ESP_OK) {
            WQMS_LOG_D("Loaded NTP servers from NVS: %s", server_list);
        }
        nvs_close(handle);
    }
}

// Load timezone from NVS
static void load_timezone_from_nvs(void) {
    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(timezone_str);
        if (nvs_get_str(handle, "timezone", timezone_str, &len) == ESP_OK) {
            WQMS_LOG_D("Loaded timezone from NVS: %s", timezone_str);
        }
        nvs_close(handle);
    }
}

// Save NTP servers to NVS
static void save_ntp_servers_to_nvs(const char *servers) {
    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, "ntp_servers", servers);
        nvs_commit(handle);
        nvs_close(handle);
        WQMS_LOG_D("NTP servers saved to NVS: %s", servers);
    }
}

// Restart NTP client with current server list
static void restart_ntp_client(void) {
    WQMS_LOG_D("Restarting NTP client with servers: %s", server_list);
    
    esp_sntp_stop();
    
    char *servers[4];
    int count = 0;
    char *saveptr;
    char *token = strtok_r(server_list, ",", &saveptr);
    while (token && count < 4) {
        servers[count] = token;
        count++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    if (count == 0) {
        for (int i = 0; i < 3; i++) {
            esp_sntp_setservername(i, default_servers[i]);
        }
    } else {
        for (int i = 0; i < count; i++) {
            esp_sntp_setservername(i, servers[i]);
        }
    }
    
    esp_sntp_init();
    
    sync_mode = NTP_MODE_FAST;
    sync_attempts = 0;
    backoff_sec = NTP_BACKOFF_BASE_SEC;
    ntp_synced = 0;
    
    WQMS_LOG_D("NTP client restarted (fast sync mode)");
}

// ============================================================
// NTP Sync Task
// ============================================================
static void ntp_sync_task(void *pvParameters) {
    ntp_task_handle = xTaskGetCurrentTaskHandle();
    WQMS_LOG_D("NTP sync task started");
    
    while (1) {
        uint32_t delay_ms = 0;
        
        if (sync_mode == NTP_MODE_FAST) {
            delay_ms = NTP_FAST_SYNC_INTERVAL_MS;   // 2000ms
        } else if (sync_mode == NTP_MODE_NORMAL) {
            delay_ms = NTP_NORMAL_SYNC_INTERVAL_SEC * 1000;  // 3600 * 1000 = 1 hour
        } else {  // BACKOFF
            delay_ms = backoff_sec * 1000;
        }
        
        // Wait for the calculated delay
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        
        // Skip if manual time is set
        if (manual_time_set) {
            continue;
        }
        
        // Only sync if we're in FAST or BACKOFF mode, or if NTP is not synced
        if (sync_mode == NTP_MODE_FAST || sync_mode == NTP_MODE_BACKOFF || !ntp_synced) {
            esp_sntp_sync_time(NULL);
        } else {
            // In NORMAL mode, just check if time is still valid
            uint32_t now = get_time_from_ntp();
            if (now > 946684800) {
                ntp_synced = 1;
            }
        }
        
        // Check sync result
        uint32_t now = get_time_from_ntp();
        if (now > 946684800) {
            ntp_synced = 1;
            sync_attempts = 0;
            backoff_sec = NTP_BACKOFF_BASE_SEC;
            
            if (sync_mode == NTP_MODE_FAST) {
                sync_mode = NTP_MODE_NORMAL;
                WQMS_LOG_I("NTP synced! Switching to normal mode (1 hour)");
            } else if (sync_mode == NTP_MODE_BACKOFF) {
                sync_mode = NTP_MODE_NORMAL;
                WQMS_LOG_I("NTP synced! Switching back to normal mode");
            }
        } else {
            ntp_synced = 0;
            sync_attempts++;
            
            if (sync_mode == NTP_MODE_FAST) {
                if (sync_attempts >= NTP_FAST_SYNC_MAX_ATTEMPTS) {
                    sync_mode = NTP_MODE_BACKOFF;
                    backoff_sec = NTP_BACKOFF_BASE_SEC;
                    WQMS_LOG_W("NTP fast sync failed after %d attempts, entering backoff", sync_attempts);
                } else {
                    WQMS_LOG_W("NTP sync attempt %d/%d failed", sync_attempts, NTP_FAST_SYNC_MAX_ATTEMPTS);
                }
            } else if (sync_mode == NTP_MODE_BACKOFF) {
                backoff_sec *= 2;
                if (backoff_sec > NTP_BACKOFF_MAX_SEC) {
                    backoff_sec = NTP_BACKOFF_MAX_SEC;
                }
                WQMS_LOG_W("NTP backoff: next sync in %lu seconds", backoff_sec);
            }
        }
        
        watchdog_heartbeat(WDT_MODULE_NTP);
    }
}

// ============================================================
// SNTP Callback
// ============================================================
static void sntp_callback(struct timeval *tv) {
    uint32_t now = get_time_from_ntp();
    if (now > 946684800) {
        ntp_synced = 1;
        WQMS_LOG_D("NTP sync callback: time = %lu", now);
    }
}

// ============================================================
// Public Functions
// ============================================================
void ntp_start(void) {
    // Load NTP servers from NVS
    load_ntp_servers_from_nvs();
    load_timezone_from_nvs();
    apply_timezone();
    
    // Set operating mode
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // Set sync interval to 1 hour (3600 seconds)
    esp_sntp_set_sync_interval(3600 * 1000);  // 1 hour in milliseconds
    
    // Parse server list
    char *servers[4];
    int count = 0;
    char *saveptr;
    char *token = strtok_r(server_list, ",", &saveptr);
    while (token && count < 4) {
        servers[count] = token;
        count++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    if (count == 0) {
        for (int i = 0; i < 3; i++) {
            esp_sntp_setservername(i, default_servers[i]);
        }
    } else {
        for (int i = 0; i < count; i++) {
            esp_sntp_setservername(i, servers[i]);
        }
    }
    
    esp_sntp_set_time_sync_notification_cb(sntp_callback);
    esp_sntp_init();
    
    xTaskCreate(ntp_sync_task, "ntp", STACK_SIZE_NTP, NULL, PRIORITY_NTP, NULL);
    
    WQMS_LOG_I("NTP client started (fast sync mode)");
}

int ntp_force_sync(void) {
    sync_mode = NTP_MODE_FAST;
    sync_attempts = 0;
    esp_sntp_restart();
    WQMS_LOG_D("NTP force sync initiated");
    return 0;
}

int ntp_is_synced(void) {
    return ntp_synced;
}

const char* ntp_get_timezone(void) {
    return timezone_str;
}

void ntp_set_timezone(const char *tz) {
    if (tz) {
        strncpy(timezone_str, tz, sizeof(timezone_str) - 1);
        timezone_str[sizeof(timezone_str) - 1] = '\0';
        
        // Apply timezone immediately
        apply_timezone();
        
        // Save to NVS
        nvs_handle_t handle;
        if (nvs_open("wqms", NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_str(handle, "timezone", timezone_str);
            nvs_commit(handle);
            nvs_close(handle);
        }
        
        WQMS_LOG_I("Timezone set to: %s (applied immediately)", timezone_str);
    }
}

const char* ntp_get_servers(void) {
    return server_list;
}

void ntp_set_servers(const char *servers) {
    if (!servers) return;
    
    strncpy(server_list, servers, sizeof(server_list) - 1);
    server_list[sizeof(server_list) - 1] = '\0';
    
    // Save to NVS
    save_ntp_servers_to_nvs(server_list);
    
    // Restart NTP client immediately with new servers
    restart_ntp_client();
    
    WQMS_LOG_D("NTP servers updated and client restarted");
}

void ntp_set_manual_time(uint32_t timestamp) {
    manual_time = timestamp;
    manual_time_set = 1;
    ntp_synced = 0;

    struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    nvs_handle_t handle;
    if (nvs_open("wqms", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, "manual_time", timestamp);
        nvs_set_u8(handle, "manual_time_set", 1);
        nvs_commit(handle);
        nvs_close(handle);
    }

    WQMS_LOG_I("Manual time set: %lu", timestamp);
}

uint32_t ntp_get_time(void) {
    time_t now = time(NULL);
    if (now < 946684800) {
        return 0;
    }
    return (uint32_t)now;
}