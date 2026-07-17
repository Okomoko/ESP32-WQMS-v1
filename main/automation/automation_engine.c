// automation_engine.c
// Rule evaluation engine implementation

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "automation_engine.h"
#include "rule_manager.h"
#include "sensor_read.h"
#include "relay_control.h"
#include "watchdog.h"
#include "log_levels.h"
#include "system_config.h"
#include "nvs_config.h"
#include "email_client.h"

// ============================================================
// Static Variables
// ============================================================
static uint32_t last_eval_time = 0;
static SemaphoreHandle_t eval_mutex = NULL;

// ============================================================
// Internal Functions
// ============================================================

// Evaluate a single condition
static int evaluate_condition(condition_t *cond) {
    sensor_reading_t *reading = sensor_get_reading(cond->sensor_id);
    if (!reading || reading->status != SENSOR_STATUS_OK) {
        return 0;  // Condition fails if sensor reading is invalid
    }
    
    float value = reading->value;
    
    switch (cond->comparator) {
        case COMP_GREATER:
            return value > cond->threshold;
        case COMP_LESS:
            return value < cond->threshold;
        case COMP_EQUAL:
            return fabs(value - cond->threshold) < 0.001f;
        case COMP_NOT_EQUAL:
            return fabs(value - cond->threshold) >= 0.001f;
        default:
            return 0;
    }
}

// Fire rule outputs
static void fire_rule_outputs(automation_rule_t *rule) {
    for (int i = 0; i < rule->output_count; i++) {
        output_t *out = &rule->outputs[i];
        
        if (out->type == OUTPUT_RELAY) {
            uint16_t duration = out->custom_duration;
            if (duration == 0) {
                // Use relay default duration
                relay_config_t *cfg = relay_get_config(out->id);
                if (cfg) {
                    duration = cfg->activity_duration;
                } else {
                    duration = 5;  // Default fallback
                }
            }
            relay_trigger_with_duration(out->id, duration);
            
            AUTO_LOG_I("Rule #%d '%s': Relay %d triggered for %d seconds",
                       rule->rule_id, rule->name, out->id, duration);
        } else if (out->type == OUTPUT_EMAIL) {
        char message[256];
        snprintf(message, sizeof(message), 
                 "Rule #%d '%s': Email notification to %s", rule->rule_id, rule->name, rule->email_recipient);
        AUTO_LOG_I(message);
        email_send_notification("Automation Alert", message);
        }
    }
}

// ============================================================
// Automation Evaluation Task
// ============================================================
static void automation_task(void *pvParameters) {
    uint32_t AUTOMATION_REFRESH_CYCLE;
    AUTOMATION_REFRESH_CYCLE = nvs_get_automation_interval();
    AUTO_LOG_I("Automation engine task started");
    
    while (1) {
        automation_evaluate();
        watchdog_heartbeat(WDT_MODULE_AUTOMATION);
        
        // Wait for next interval (in seconds)
        vTaskDelay(pdMS_TO_TICKS(AUTOMATION_REFRESH_CYCLE * 1000));
    }
}

// ============================================================
// Public Functions
// ============================================================

void automation_init(void) {
    // Initialize rule manager
    rule_manager_init();
    
    // Create mutex
    eval_mutex = xSemaphoreCreateMutex();
    if (!eval_mutex) {
        AUTO_LOG_E("Failed to create automation mutex");
        return;
    }
    
    // Start automation task
    xTaskCreate(automation_task, "automation", STACK_SIZE_AUTOMATION, NULL, PRIORITY_AUTOMATION, NULL);
    
    // Register with watchdog
    watchdog_register_module(WDT_MODULE_AUTOMATION, 3);
    
    AUTO_LOG_I("Automation engine initialized");
}

void automation_evaluate(void) {
    if (eval_mutex && xSemaphoreTake(eval_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    last_eval_time = esp_timer_get_time() / 1000000ULL;
    
    automation_rule_t *rules = rule_get_all();
//    int count = rule_get_count();
    
    for (int i = 0; i < MAX_RULES; i++) {
        if (rules[i].name[0] == '\0') continue;
        if (!rules[i].enabled) continue;
        
        // Check cooldown
        if (rules[i].cooldown_enabled && rules[i].cooldown_seconds > 0) {
            uint32_t elapsed = last_eval_time - rules[i].last_triggered;
            if (elapsed < rules[i].cooldown_seconds) {
                // Skip due to cooldown
                continue;
            }
        }
        
        // Evaluate conditions
        int all_true = 1;
        for (int j = 0; j < rules[i].condition_count; j++) {
            int result = evaluate_condition(&rules[i].conditions[j]);
            
            if (rules[i].logic_type == LOGIC_AND) {
                all_true = all_true && result;
            } else if (rules[i].logic_type == LOGIC_OR) {
                all_true = all_true || result;
            }
            
            if (!all_true && rules[i].logic_type == LOGIC_AND) {
                break;  // Short-circuit
            }
            if (all_true && rules[i].logic_type == LOGIC_OR) {
                break;  // Short-circuit
            }
        }
        
        // Fire if conditions met
        if (all_true) {
            fire_rule_outputs(&rules[i]);
            rules[i].last_triggered = last_eval_time;
            rules[i].trigger_count++;
            
            // Log the firing
            char desc[128];
            rule_get_description(&rules[i], desc, sizeof(desc));
            AUTO_LOG_I("Rule #%d '%s' fired: %s", i, rules[i].name, desc);
        } else {
            AUTO_LOG_I("Rule #%d '%s' is not fired!", i, rules[i].name);
        }
    }
    
    xSemaphoreGive(eval_mutex);
}

int automation_test_rule(uint8_t rule_id) {
    // Force evaluation of a single rule (for testing)
    automation_rule_t *rule = rule_get(rule_id);
    if (!rule || rule->name[0] == '\0') {
        return -1;
    }
    
    // Save current enabled state, force enable
    uint8_t was_enabled = rule->enabled;
    rule->enabled = 1;
    
    // Evaluate just this rule
    // (This is a simplified version - in production we'd have a separate function)
    automation_evaluate();
    
    // Restore state
    rule->enabled = was_enabled;
    
    AUTO_LOG_I("Rule #%d tested", rule_id);
    return 0;
}

uint32_t automation_get_last_eval_time(void) {
    return last_eval_time;
}