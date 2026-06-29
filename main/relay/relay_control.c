// relay_control.c
// Relay control implementation - 12 outputs with activity duration and cooldown

#include <string.h>
#include <time.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "relay_control.h"
#include "nvs_config.h"
#include "watchdog.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"

#define TAG "RELAY"

// ============================================================
// Static Variables
// ============================================================
static relay_instance_t relays[RELAY_COUNT];
static SemaphoreHandle_t relay_mutex = NULL;
static int relay_initialized = 0;

// ============================================================
// Timer Callbacks
// ============================================================
static void relay_activity_timer_callback(TimerHandle_t xTimer) {
    uint8_t relay_id = (uint8_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    if (relay_id >= RELAY_COUNT) {
        ESP_LOGE(TAG, "Invalid relay ID in callback: %d", relay_id);
        return;
    }
    
    relay_instance_t *relay = &relays[relay_id];
    // Validate that this timer matches the one in our struct
    if (relay->activity_timer != xTimer) {
        ESP_LOGE(TAG, "Timer handle mismatch for relay %d", relay_id);
        return;
    }
    
    if (relay_mutex && xSemaphoreTake(relay_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Turn relay OFF
        gpio_set_level(relay->config.gpio_pin, RELAY_STATE_IDLE);
        relay->state = RELAY_STATE_IDLE;
        relay->state_start_time = 0;
        
        ESP_LOGI(TAG, "Relay %d (%s) turned OFF.", relay_id, relay->config.name);
        
        // Start cooldown timer if off_delay > 0
        if (relay->config.off_delay > 0) {
            relay->state = RELAY_STATE_COOLDOWN;
            relay->state_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
            xTimerStart(relay->cooldown_timer, 0);
        }

        xSemaphoreGive(relay_mutex);
    }
}

static void relay_cooldown_timer_callback(TimerHandle_t xTimer) {
    uint8_t relay_id = (uint8_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    if (relay_id >= RELAY_COUNT) return;
    
    relay_instance_t *relay = &relays[relay_id];
    
    if (relay_mutex && xSemaphoreTake(relay_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        relay->state = RELAY_STATE_IDLE;
        relay->state_start_time = 0;
        ESP_LOGI(TAG, "Relay %d (%s) cooldown ended", relay_id, relay->config.name);
        xSemaphoreGive(relay_mutex);
    }
}

// ============================================================
// Internal Functions
// ============================================================
static int relay_trigger_internal(uint8_t relay_id, uint16_t duration_ms) {
    if (relay_id >= RELAY_COUNT) return -1;
    if (!relay_initialized) return -2;
    
    relay_instance_t *relay = &relays[relay_id];
    
    if (!relay->config.enabled) {
        ESP_LOGW(TAG, "Relay %d is disabled", relay_id);
        return -3;
    }
    
    if (relay->state == RELAY_STATE_ACTIVE) {
        ESP_LOGW(TAG, "Relay %d already active", relay_id);
        return -4;
    }
    
    if (relay->state == RELAY_STATE_COOLDOWN) {
        ESP_LOGW(TAG, "Relay %d in cooldown", relay_id);
        return -5;
    }
    
    // Turn relay ON
    gpio_set_level(relay->config.gpio_pin, RELAY_STATE_ACTIVE);
    relay->state = RELAY_STATE_ACTIVE;
    relay->state_start_time = esp_timer_get_time() / 1000000ULL;
    
    // Determine duration (custom or default), clamp to min/max
    uint16_t dur = (duration_ms > 0) ? duration_ms : relay->config.activity_duration;
    if (dur < RELAY_MIN_DURATION_MS) dur = RELAY_MIN_DURATION_MS;
    if (dur > RELAY_MAX_DURATION_MS) dur = RELAY_MAX_DURATION_MS;
    // Start the timer
    xTimerChangePeriod(relay->activity_timer, pdMS_TO_TICKS(dur), 0);
    xTimerStart(relay->activity_timer, 0);

    ESP_LOGI(TAG, "Relay %d (%s) triggered ON for %d ms", relay_id, relay->config.name, dur);

    return 0;
}

// ============================================================
// Public Functions
// ============================================================

void relay_init(void) {
    relay_mutex = xSemaphoreCreateMutex();
    if (!relay_mutex) {
        WQMS_LOG_E("Failed to create relay mutex");
        return;
    }
    
    // ✅ Load directly from NVS (no config_loader)
    relay_config_t configs[RELAY_COUNT];
    nvs_load_relay_config(configs, RELAY_COUNT);
    
    // Initialize each relay
    for (int i = 0; i < RELAY_COUNT; i++) {
        // Copy config
        memcpy(&relays[i].config, &configs[i], sizeof(relay_config_t));
        relays[i].state = RELAY_STATE_IDLE;
        relays[i].state_start_time = 0;
        
        // Set GPIO pin
        gpio_set_direction(relays[i].config.gpio_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(relays[i].config.gpio_pin, RELAY_STATE_IDLE);
        
        // ============================================================
        // Validate Activity Duration (clamp to min/max)
        // ============================================================
        uint16_t duration = relays[i].config.activity_duration;
        if (duration < RELAY_MIN_DURATION_MS) {
            duration = RELAY_MIN_DURATION_MS;
            relays[i].config.activity_duration = duration;
            WQMS_LOG_W("Relay %d duration clamped to %d ms", i, duration);
        }
        if (duration > RELAY_MAX_DURATION_MS) {
            duration = RELAY_MAX_DURATION_MS;
            relays[i].config.activity_duration = duration;
            WQMS_LOG_W("Relay %d duration clamped to %d ms", i, duration);
        }
        
        // ============================================================
        // Validate Off-Delay (can be 0, but clamp if > 0 and < min)
        // ============================================================
        uint16_t off_delay = relays[i].config.off_delay;
        if (off_delay > 0 && off_delay < RELAY_MIN_DURATION_MS) {
            off_delay = RELAY_MIN_DURATION_MS;
            relays[i].config.off_delay = off_delay;
            WQMS_LOG_W("Relay %d off-delay clamped to %d ms", i, off_delay);
        }
        if (off_delay > RELAY_MAX_DURATION_MS) {
            off_delay = RELAY_MAX_DURATION_MS;
            relays[i].config.off_delay = off_delay;
            WQMS_LOG_W("Relay %d off-delay clamped to %d ms", i, off_delay);
        }
        
        // ============================================================
        // Create Activity Timer (must be > 0 ticks)
        // ============================================================
        char name[32];
        snprintf(name, sizeof(name), "act_relay_%d", i);
        relays[i].activity_timer = xTimerCreate(name,
                                                pdMS_TO_TICKS(duration),
                                                pdFALSE,
                                                (void*)(uintptr_t)i,
                                                relay_activity_timer_callback);
        if (relays[i].activity_timer == NULL) {
            WQMS_LOG_E("Failed to create activity timer for relay %d", i);
        }

        // ============================================================
        // Create Cooldown Timer (only if off_delay > 0)
        // ============================================================
        if (off_delay > 0) {
            snprintf(name, sizeof(name), "cool_relay_%d", i);
            relays[i].cooldown_timer = xTimerCreate(name,
                                                    pdMS_TO_TICKS(off_delay),
                                                    pdFALSE,
                                                    (void*)(uintptr_t)i,
                                                    relay_cooldown_timer_callback);
            if (relays[i].cooldown_timer == NULL) {
                WQMS_LOG_W("Failed to create cooldown timer for relay %d", i);
            }
        } else {
            relays[i].cooldown_timer = NULL;
        }
        
        WQMS_LOG_D("Relay %d initialized: GPIO %d, duration %d ms, off_delay %d ms",
                   i, relays[i].config.gpio_pin,
                   relays[i].config.activity_duration,
                   relays[i].config.off_delay);
    }
    
    relay_initialized = 1;
    WQMS_LOG_I("Relay subsystem initialized (%d relays)", RELAY_COUNT);
}

relay_config_t* relay_get_config(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return NULL;
    return &relays[relay_id].config;
}

int relay_get_state(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return -1;
    return relays[relay_id].state;
}

int relay_trigger(uint8_t relay_id) {
    return relay_trigger_internal(relay_id, 0);
}

int relay_trigger_with_duration(uint8_t relay_id, uint16_t duration_ms) {
    return relay_trigger_internal(relay_id, duration_ms);
}

int relay_on(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return -1;
    if (!relay_initialized) return -2;
    
    relay_instance_t *relay = &relays[relay_id];
    if (!relay->config.enabled) return -3;
    
    // Cancel any running timers
    xTimerStop(relay->activity_timer, 0);
    if (relay->cooldown_timer) {
        xTimerStop(relay->cooldown_timer, 0);
    }
    
    gpio_set_level(relay->config.gpio_pin, RELAY_STATE_ACTIVE);
    relay->state = RELAY_STATE_ACTIVE;
    relay->state_start_time = esp_timer_get_time() / 1000000ULL;
    
    ESP_LOGI(TAG, "Relay %d (%s) manually ON", relay_id, relay->config.name);
    return 0;
}

int relay_off(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return -1;
    if (!relay_initialized) return -2;
    
    relay_instance_t *relay = &relays[relay_id];
    
    // Cancel timers
    xTimerStop(relay->activity_timer, 0);
    if (relay->cooldown_timer) {
        xTimerStop(relay->cooldown_timer, 0);
    }
    
    gpio_set_level(relay->config.gpio_pin, RELAY_STATE_IDLE);
    relay->state = RELAY_STATE_IDLE;
    relay->state_start_time = 0;
    
    ESP_LOGI(TAG, "Relay %d (%s) manually OFF", relay_id, relay->config.name);
    return 0;
}

int relay_is_active(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return 0;
    return (relays[relay_id].state == RELAY_STATE_ACTIVE);
}

uint32_t relay_get_remaining(uint8_t relay_id) {
    if (relay_id >= RELAY_COUNT) return 0;
    if (relays[relay_id].state != RELAY_STATE_ACTIVE) return 0;
    
    uint32_t now = esp_timer_get_time() / 1000000ULL;
    uint32_t elapsed = now - relays[relay_id].state_start_time;
    uint32_t duration = relays[relay_id].config.activity_duration;
    
    if (elapsed >= duration) return 0;
    return duration - elapsed;
}

// ============================================================
// Relay Config Reload (FIXED - Properly handles timer lifecycle)
// ============================================================
void relay_reload_config(void) {
    if (!relay_initialized) {
        WQMS_LOG_W("Relay not initialized, cannot reload config");
        return;
    }
    
    WQMS_LOG_I("Reloading relay configuration...");
    
    // Step 1: Stop and delete ALL existing timers
    for (int i = 0; i < RELAY_COUNT; i++) {
        // Stop and delete activity timer
        if (relays[i].activity_timer) {
            xTimerStop(relays[i].activity_timer, 0);
            xTimerDelete(relays[i].activity_timer, 0);
            relays[i].activity_timer = NULL;
        }
        
        // Stop and delete cooldown timer
        if (relays[i].cooldown_timer) {
            xTimerStop(relays[i].cooldown_timer, 0);
            xTimerDelete(relays[i].cooldown_timer, 0);
            relays[i].cooldown_timer = NULL;
        }
    }
    
    // Step 2: Load new config from NVS
    relay_config_t configs[RELAY_COUNT];
    nvs_load_relay_config(configs, RELAY_COUNT);
    
    // Step 3: Reinitialize each relay with new timers
    for (int i = 0; i < RELAY_COUNT; i++) {
        // Copy config
        memcpy(&relays[i].config, &configs[i], sizeof(relay_config_t));
        relays[i].state = RELAY_STATE_IDLE;
        relays[i].state_start_time = 0;
        
        // Validate duration
        uint16_t duration = relays[i].config.activity_duration;
        if (duration < RELAY_MIN_DURATION_MS) {
            duration = RELAY_MIN_DURATION_MS;
            relays[i].config.activity_duration = duration;
        }
        if (duration > RELAY_MAX_DURATION_MS) {
            duration = RELAY_MAX_DURATION_MS;
            relays[i].config.activity_duration = duration;
        }
        
        // Validate off-delay
        uint16_t off_delay = relays[i].config.off_delay;
        if (off_delay > 0 && off_delay < RELAY_MIN_DURATION_MS) {
            off_delay = RELAY_MIN_DURATION_MS;
            relays[i].config.off_delay = off_delay;
        }
        if (off_delay > RELAY_MAX_DURATION_MS) {
            off_delay = RELAY_MAX_DURATION_MS;
            relays[i].config.off_delay = off_delay;
        }
        
        // Set GPIO pin (in case it changed)
        gpio_set_direction(relays[i].config.gpio_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(relays[i].config.gpio_pin, RELAY_STATE_IDLE);
        
        // Create new activity timer
        char name[32];
        snprintf(name, sizeof(name), "act_relay_%d", i);
        relays[i].activity_timer = xTimerCreate(
            name,
            pdMS_TO_TICKS(duration),
            pdFALSE,
            (void*)(uintptr_t)i,
            relay_activity_timer_callback
        );
        if (relays[i].activity_timer == NULL) {
            WQMS_LOG_E("Failed to create activity timer for relay %d", i);
        }
        
        // Create new cooldown timer (only if off_delay > 0)
        if (off_delay > 0) {
            snprintf(name, sizeof(name), "cool_relay_%d", i);
            relays[i].cooldown_timer = xTimerCreate(
                name,
                pdMS_TO_TICKS(off_delay),
                pdFALSE,
                (void*)(uintptr_t)i,
                relay_cooldown_timer_callback
            );
            if (relays[i].cooldown_timer == NULL) {
                WQMS_LOG_W("Failed to create cooldown timer for relay %d", i);
            }
        } else {
            relays[i].cooldown_timer = NULL;
        }
        
        WQMS_LOG_D("Relay %d reloaded: GPIO %d, duration %d ms, off_delay %d ms",
                   i, relays[i].config.gpio_pin,
                   relays[i].config.activity_duration,
                   relays[i].config.off_delay);
    }
    
    WQMS_LOG_I("Relay configuration reloaded successfully");
}