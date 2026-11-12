// main.c
// System entry point - minimal boot sequence

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"

#include "project_defs.h"
#include "system_config.h"
#include "log_levels.h"
#include "logger.h"
#include "log_rotate.h"
#include "watchdog.h"
#include "littlefs_manager.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "email_client.h"
#include "relay_control.h"
#include "web_console.h"
#include "supabase_upload.h"
#include "cert_manager.h"
#include "internal_led.h"

static char supabase_cert_buffer[SSL_CERTIFICATE_MAX_SIZE];

// ============================================================
// External function prototypes
// ============================================================
void sensor_init(void);
void relay_init(void);
void ntp_start(void);
void modbus_init(void);
void webserver_init(void);
void automation_init(void);

void check_and_validate_ota_app(void)
{
    // 1. Şu anda hangi partition slotundan (ota_0 mı ota_1 mi) çalıştığımızı öğreniyoruz
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    WQMS_LOG_I("Active application partition slot: %s", running_partition->label);

    // 2. Bir önceki resetin nedenini kontrol ediyoruz
    esp_reset_reason_t reason = esp_reset_reason();

    // 3. Eğer cihaz bir yazılım çökmesi (Watchdog, Panic vb.) yüzünden resetlendiyse
    // ve henüz bu yeni sürüm onaylanmadıysa, sistem zaten otomatik rollback sürecindedir.
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT) {
        WQMS_LOG_W("System has been rebooted due to a application crash! Rollback might be triggered.");
    }

    // 4. OTA Durumunu alıyoruz (Bu uygulama onaylandı mı, deneme aşamasında mı?)
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Eğer durum "PENDING_VERIFY" ise: Bu yazılım ilk defa boot ediyor demektir!
            WQMS_LOG_W("New version of OTA software is running for the first time. Waiting for verification...");
            
            /* 
             * [KRİTİK NOT]: Eğer burada Wi-Fi bağlantısı veya HTTP sunucusu gibi servislerinizin
             * ayağa kalktığını doğrulamak isterseniz, bu fonksiyonu o servisler başarıyla 
             * bağlandıktan SONRA tetikleyebilirsiniz. 
             */
            
            // 5. Yazılımın başarılı çalıştığını otadata alanına onaylıyoruz (Rollback'i iptal eder)
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                WQMS_LOG_I("New version of OTA software has been verified succesfully. It is now the current active version.");
            } else {
                WQMS_LOG_E("There is an error during verification! Code: 0x%x", err);
            }
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            WQMS_LOG_I("This version has already been verified before, it is the current stable version.");
        }
    }
}

void json_or_http_ota_task(void *pvParameter)
{
    WQMS_LOG_I("OTA update is in progress...");

    char url[270]="";
    char bucketdownloadurl[256]="";
    nvs_get_supabase_bucketdownload_url(bucketdownloadurl, sizeof(bucketdownloadurl));
    snprintf(url, sizeof(url), "%s/%s", bucketdownloadurl, OTA_FIRMWARE_URL);
    if (strlen(bucketdownloadurl) == 0) {
        WQMS_LOG_E("No Supabase URL in NVS, aborting...");
        vTaskDelete(NULL);
    }

    // Load certificate from NVS using generic manager
    size_t cert_len = 0;

    if (cert_manager_has(CERT_TYPE_SUPABASE)) {
        esp_err_t err = cert_manager_load(CERT_TYPE_SUPABASE, supabase_cert_buffer, SSL_CERTIFICATE_MAX_SIZE, &cert_len);
        if (err == ESP_OK && cert_len > 0) {
            supabase_cert_buffer[cert_len] = 0;
            supabase_cert_buffer[cert_len + 1] = 0;
//            WQMS_LOG_D("Using Supabase certificate from NVS (%zu bytes)", cert_len);
        } else {
            WQMS_LOG_W("Failed to load Supabase certificate: %s", esp_err_to_name(err));
            vTaskDelete(NULL);
        }
    } else {
        WQMS_LOG_E("No Supabase certificate in NVS, aborting...");
        vTaskDelete(NULL);
    }
    
    // 1. HTTP İstemci Yapılandırması
    esp_http_client_config_t config = {
        .url = url,
#if CONFIG_OTA_USE_HTTPS
        .cert_pem = supabase_cert_buffer, // HTTPS için sertifika bağlama
        .cert_len = cert_len + 1,
#endif
        .timeout_ms = 8000,                         // Sunucu yanıt zaman aşımı (8 saniye)
        .keep_alive_enable = true,
    };

    // 2. HTTPS/HTTP OTA Yapılandırması
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        // v6.0 standartlarında ek güvenlik ve opsiyonel ayarlar buraya eklenebilir
    };

    WQMS_LOG_I("Connecting to the server and checking the firmware: %s", OTA_FIRMWARE_URL);
    
    // 3. Güncellemeyi Başlat ve Arka Planda Yönet
    esp_err_t ret = esp_https_ota(&ota_config);
    
    if (ret == ESP_OK) {
        WQMS_LOG_I("Firmware is downloaded succesfully and unit is about to reboot in seconds...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart(); // Cihazı resetle -> Bootloader otomatik olarak yeni slottan (ota_1) baslatacak.
    } else {
        WQMS_LOG_E("OTA update has failed! Error code: 0x%x", ret);
        WQMS_LOG_I("Still keeping the old - stable firmware version as a current version.");
        
        // Görevi sonlandır (Task kendini imha eder)
        vTaskDelete(NULL);
    }
}

// Bu fonksiyonu Wi-Fi bağlantısı TAMAMLANDIKTAN sonra çağırmalısınız.
void start_ota_update(void)
{
    // OTA indirme işlemi FreeRTOS üzerinde ayrı bir task (görev) olarak koşturulur.
    xTaskCreate(&json_or_http_ota_task, "ota_update_task", STACK_SIZE_OTA, NULL, PRIORITY_OTA, NULL);
    WQMS_LOG_I("OTA Update task is created.");
}

// ============================================================
// Main Entry Point
// ============================================================
void app_main(void) {
    Internal_LED_Init();
    // Cihaz açılır açılmaz ilk iş OTA durumunu kontrol edip onay veriyoruz
    Internal_LED_Blink (3, LED_COLOR_RED);
    check_and_validate_ota_app();
    Internal_LED_Blink (3, LED_COLOR_ORANGE);

    // ============================================================
    // Step 0: Start web console interface
    // ============================================================
    ESP_ERROR_CHECK(web_console_init());
    Internal_LED_Blink (3, LED_COLOR_YELLOW);

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
    Internal_LED_Blink (3, LED_COLOR_GREEN);
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
    ret = nvs_get_datetime("datetime");
    WQMS_LOG_I("NVS initialized");

    // ============================================================
    // Step 2: Initialize LittleFS (webpartition, logs, sensors)
    // ============================================================
    Internal_LED_Blink (3, LED_COLOR_CYAN);
    littlefs_init();

    // ============================================================
    // Step 3: Initialize Logging System
    // ============================================================
    Internal_LED_Blink (3, LED_COLOR_BLUE);
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
    Internal_LED_Blink (3, LED_COLOR_MAGENTA);
    watchdog_init();
//    WQMS_LOG_I("Watchdog initialized");
    
    // ============================================================
    // Step 5: Load System Configuration from NVS
    // ============================================================
    Internal_LED_Blink (3, LED_COLOR_PURPLE);
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
    vTaskDelay(pdMS_TO_TICKS(3000));
    ret = nvs_save_datetime("datetime", NULL);

    // ============================================================
    // Step 9: Initialize OTA Update
    // ============================================================

    start_ota_update();

    // ============================================================
    // Step 10: Initialize Supabase upload system
    // ============================================================

    WQMS_LOG_I("=== Initializing Supabase Upload ===");
    
    ret = supabase_upload_init();
    if (ret == ESP_OK) {
        ret = supabase_upload_start();
        if (ret != ESP_OK) {
            WQMS_LOG_E("Failed to start Supabase upload: %s", esp_err_to_name(ret));
        }
    } else {
        WQMS_LOG_E("Failed to initialize Supabase upload: %s", esp_err_to_name(ret));
    }

    // ============================================================
    // Step 11: Start Web Server
    // ============================================================
    webserver_init();
//    WQMS_LOG_I("Web server initialized");

    // ============================================================
    // Step 12: Start eMail Client
    // ============================================================
//vTaskDelay(pdMS_TO_TICKS(30000));
    email_client_init();
    // Send email notification if it was a panic or watchdog reset
    if (is_panic_reboot) {
        char message[256];
        snprintf(message, sizeof(message), 
                 "⚠️ Device rebooted due to: %s\n",
                 reason_str);
        
        ret = email_send_notification("Device Rebooted", 
                                   message);
    } else {
        char message[256];
        snprintf(message, sizeof(message), 
                 "⚠️ Device is just booted up.\n");
        
        ret = email_send_notification("Device Rebooted", 
                                   message);
    }

    if (ret == ESP_OK) {
        WQMS_LOG_I("Reboot notification is sent.");
    } else {
        WQMS_LOG_E("Reboot notification cannot be sent.");
    }

    // ============================================================
    // Step 13: Start Relay Control
    // ============================================================
    relay_init();
//    WQMS_LOG_I("Relay subsystem initialized");
    
    // ============================================================
    // Step 14: Start Sensor Polling (ADC DMA)
    // ============================================================
    sensor_init();
//    WQMS_LOG_I("Sensor subsystem initialized");
    
    // ============================================================
    // Step 15: Start Automation Engine
    // ============================================================
    automation_init();
//    WQMS_LOG_I("Automation engine initialized");
    
    // ============================================================
    // Step 16: Start MODBUS
    // ============================================================
    modbus_init();
//    WQMS_LOG_I("MODBUS is initialized");
    
    // ============================================================
    // Step 17: System Ready
    // ============================================================
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("WQMS System Ready");
    WQMS_LOG_I("============================================");
    WQMS_LOG_I("Free heap: %lu bytes", esp_get_free_heap_size());
    Internal_LED_Rainbow(10);
//    int led_gpio = 1;
//    gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);

    // Send heartbeat to watchdog (main task)
    int8_t minute_update = 0;
    int8_t hour_update = 0;
    while (1) {
        watchdog_heartbeat(WDT_MODULE_LOGGING);
//        gpio_set_level(led_gpio, RELAY_STATE_IDLE);
//        WQMS_LOG_I("GPIO%d : %d", led_gpio, gpio_get_level(led_gpio));
        vTaskDelay(pdMS_TO_TICKS(500));
//        gpio_set_level(led_gpio, RELAY_STATE_ACTIVE);
//        WQMS_LOG_I("GPIO%d : %d", led_gpio, gpio_get_level(led_gpio));
        vTaskDelay(pdMS_TO_TICKS(500));
        minute_update ++;
        if (minute_update == 60) {
            ret = nvs_save_datetime("datetime", NULL);
            minute_update = 0;
            hour_update ++;
            if (hour_update == 60) {
                supabase_sync_sensor_config();
                hour_update = 0;
            };
        }
    }
}
