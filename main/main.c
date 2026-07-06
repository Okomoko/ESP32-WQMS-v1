// main.c
// System entry point - minimal boot sequence

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
//#include "esp_reset_reason.h"  // or just include esp_system.h

#include "project_defs.h"
#include "system_config.h"
#include "log_levels.h"
#include "logger.h"
#include "log_rotate.h"
#include "watchdog.h"
#include "spiffs_manager.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "email_client.h"

// ============================================================
// System Version
// ============================================================
#define FW_VERSION "v1.0.0"

// ============================================================
// External function prototypes
// ============================================================
void sensor_init(void);
void relay_init(void);
void ntp_start(void);
void modbus_init(void);
void webserver_init(void);
void automation_init(void);
void web_upload_init(void);

// ============================================================
// Main Entry Point
// ============================================================
void app_main(void) {
    // Check reset reason right at boot
    esp_reset_reason_t reason = esp_reset_reason();
    
    bool is_panic_reboot = false;
    const char* reason_str = "Unknown";
    
    switch(reason) {
        case ESP_RST_PANIC:
            reason_str = "Panic/Exception";
            is_panic_reboot = true;
            break;
        case ESP_RST_TASK_WDT:
            reason_str = "Task Watchdog Timeout";
            is_panic_reboot = true; // Treat as critical
            break;
        case ESP_RST_INT_WDT:
            reason_str = "Interrupt Watchdog Timeout";
            is_panic_reboot = true;
            break;
        case ESP_RST_POWERON:
            reason_str = "Power-on";
            break;
        case ESP_RST_SW:
            reason_str = "Software restart";
            break;
        case ESP_RST_BROWNOUT:
            reason_str = "Brownout";
            is_panic_reboot = true; // Power issue
            break;
        default:
            reason_str = "Other/Unknown";
            break;
    }
    
    WQMS_LOG_I("Last reset reason: %s (code: %d)", reason_str, reason);
    
    // ============================================================
    // Step 1: Initialize NVS
    // ============================================================
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        WQMS_LOG_W("NVS corrupt, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        WQMS_LOG_E("NVS init failed: %d", ret);
        return;
    }
    WQMS_LOG_I("NVS initialized");

    // ============================================================
    // Step 2: Initialize SPIFFS (webpartition, logs, sensors)
    // ============================================================
    spiffs_init();

    // ============================================================
    // Step 3: Initialize Logging System
    // ============================================================
    log_init();
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("WQMS System Starting - %s", FW_VERSION);
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("Build: %s %s", __DATE__, __TIME__);
    
    // Get CPU frequency and flash size
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    WQMS_LOG_I("CPU: %d MHz, Flash: %d MB", 
               CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
               flash_size / (1024 * 1024));
    
    // ============================================================
    // Step 4: Initialize Watchdog
    // ============================================================
    watchdog_init();
//    WQMS_LOG_I("Watchdog initialized");
    
    // ============================================================
    // Step 5: Load System Configuration from NVS
    // ============================================================
    nvs_config_load();
    
    // ============================================================
    // Step 6: Initialize Network
    // ============================================================
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // ============================================================
    // Step 7: Start WiFi (STA with AP fallback)
    // ============================================================
    wifi_init();
//    WQMS_LOG_I("WiFi initialized");
    
    // ============================================================
    // Step 8: Start NTP Client
    // ============================================================
    ntp_start();
//    WQMS_LOG_I("NTP client started");
  
    // ============================================================
    // Step 9: Start eMail Client
    // ============================================================
	email_client_init();
    // Send email notification if it was a panic or watchdog reset
    if (is_panic_reboot) {
        char message[256];
        snprintf(message, sizeof(message), 
                 "⚠️ Device rebooted due to: %s\n",
                 reason_str);
        
        email_send_notification("Device Rebooted", 
                                   message);
    } else {
        char message[256];
        snprintf(message, sizeof(message), 
                 "⚠️ Device is just booted up.\n");
        
        email_send_notification("Device Rebooted", 
                                   message);
    }
    WQMS_LOG_I("Reboot notification is sent.");

    // ============================================================
    // Step 10: Start Sensor Polling (ADC DMA)
    // ============================================================
    sensor_init();
//    WQMS_LOG_I("Sensor subsystem initialized");
    
    // ============================================================
    // Step 11: Start Relay Control
    // ============================================================
    relay_init();
//    WQMS_LOG_I("Relay subsystem initialized");
    
    // ============================================================
    // Step 12: Start Automation Engine
    // ============================================================
    automation_init();
//    WQMS_LOG_I("Automation engine initialized");
    
    // ============================================================
    // Step 13: Start MODBUS Slave
    // ============================================================
    modbus_init();
//    WQMS_LOG_I("MODBUS slave initialized");
    
    // ============================================================
    // Step 14: Start Web Server
    // ============================================================
    webserver_init();
//    WQMS_LOG_I("Web server initialized");
    
    // ============================================================
    // Step 15: Start Integration Uploads
    // ============================================================
    web_upload_init();
//    WQMS_LOG_I("Integration subsystem initialized");
    
    // ============================================================
    // Step 16: System Ready
    // ============================================================
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("WQMS System Ready");
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Send heartbeat to watchdog (main task)
    while (1) {
        watchdog_heartbeat(WDT_MODULE_LOGGING);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
