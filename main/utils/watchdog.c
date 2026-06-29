// watchdog.c
// Module heartbeat watchdog implementation

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "watchdog.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

// ============================================================
// Static Structures
// ============================================================
typedef struct {
    wdt_module_t module;
    uint32_t timeout_sec;
    uint32_t last_heartbeat;
    uint32_t registered;
} wdt_entry_t;

// ============================================================
// Static Variables
// ============================================================
static wdt_entry_t wdt_registry[WDT_MODULE_MAX];
static int wdt_registered_count = 0;
static SemaphoreHandle_t wdt_mutex = NULL;
static const char *module_names[] = {
    "SENSOR",
    "AUTOMATION",
    "MODBUS",
    "WEBSERVER",
    "INTEGRATION",
    "NTP",
    "LOGGING",
    "WIFI"
};

// ============================================================
// Internal Helper
// ============================================================
static const char* get_module_name(wdt_module_t module) {
    if (module < WDT_MODULE_MAX) {
        return module_names[module];
    }
    return "UNKNOWN";
}

// ============================================================
// Public Functions
// ============================================================

void watchdog_init(void) {
    wdt_mutex = xSemaphoreCreateMutex();
    if (wdt_mutex == NULL) {
        WQMS_LOG_E("Failed to create watchdog mutex");
        return;
    }
    
    for (int i = 0; i < WDT_MODULE_MAX; i++) {
        wdt_registry[i].registered = 0;
        wdt_registry[i].last_heartbeat = 0;
        wdt_registry[i].timeout_sec = 30;
    }
    
    WQMS_LOG_I("Watchdog initialized");
}

void watchdog_register_module(wdt_module_t module, uint32_t timeout_sec) {
    if (module >= WDT_MODULE_MAX) {
        WQMS_LOG_E("Invalid module ID: %d", module);
        return;
    }
    
    if (wdt_mutex && xSemaphoreTake(wdt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        wdt_registry[module].module = module;
        wdt_registry[module].timeout_sec = timeout_sec;
        wdt_registry[module].last_heartbeat = esp_timer_get_time() / 1000000ULL;
        wdt_registry[module].registered = 1;
        wdt_registered_count++;
        xSemaphoreGive(wdt_mutex);
        WQMS_LOG_D("Module %s registered with timeout %lu s", get_module_name(module), timeout_sec);
    } else {
        WQMS_LOG_E("Failed to register module %s", get_module_name(module));
    }
}

void watchdog_heartbeat(wdt_module_t module) {
    if (module >= WDT_MODULE_MAX) return;
    
    if (wdt_mutex && xSemaphoreTake(wdt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        wdt_registry[module].last_heartbeat = esp_timer_get_time() / 1000000ULL;
        wdt_registry[module].registered = 1;
        xSemaphoreGive(wdt_mutex);
    }
}

uint32_t watchdog_get_elapsed(wdt_module_t module) {
    if (module >= WDT_MODULE_MAX) return 0;
    
    uint32_t now = esp_timer_get_time() / 1000000ULL;
    if (wdt_mutex && xSemaphoreTake(wdt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t elapsed = now - wdt_registry[module].last_heartbeat;
        xSemaphoreGive(wdt_mutex);
        return elapsed;
    }
    return 0;
}

uint32_t watchdog_get_timeout(wdt_module_t module) {
    if (module >= WDT_MODULE_MAX) return 0;
    return wdt_registry[module].timeout_sec;
}

int watchdog_all_healthy(void) {
    uint32_t now = esp_timer_get_time() / 1000000ULL;
    int all_ok = 1;
    
    if (wdt_mutex && xSemaphoreTake(wdt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < WDT_MODULE_MAX; i++) {
            if (!wdt_registry[i].registered) continue;
            uint32_t elapsed = now - wdt_registry[i].last_heartbeat;
            if (elapsed > wdt_registry[i].timeout_sec) {
                WQMS_LOG_E("Watchdog: Module %s timeout (%lu s > %lu s)",
                           get_module_name(i), elapsed, wdt_registry[i].timeout_sec);
                all_ok = 0;
            }
        }
        xSemaphoreGive(wdt_mutex);
    }
    return all_ok;
}
