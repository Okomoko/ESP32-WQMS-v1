// web_upload_task.c
// Periodic upload task for web integration

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "web_upload.h"
#include "buffer_recovery.h"
#include "nvs_config.h"
#include "watchdog.h"
#include "log_levels.h"

// ============================================================
// Upload Task
// ============================================================
static void web_upload_task(void *pvParameters) {
    INTEGRATION_LOG_I("Web upload task started");
    
    while (1) {
        // 1. Check if we have pending uploads to retry
        if (buffer_recovery_get_pending_count() > 0) {
            INTEGRATION_LOG_I("Retrying %d pending uploads", buffer_recovery_get_pending_count());
            buffer_recovery_retry_all();
        }
        
        // 2. Upload current sensor data
        web_upload_sensors();
        
        // 3. Watchdog heartbeat
        watchdog_heartbeat(WDT_MODULE_INTEGRATION);
        
        // 4. Wait for interval
        uint32_t interval = nvs_get_integration_interval();
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
}

void start_web_upload_task(void) {
    xTaskCreate(web_upload_task, "web_upload", STACK_SIZE_INTEGRATION, NULL, PRIORITY_INTEGRATION, NULL);
}
