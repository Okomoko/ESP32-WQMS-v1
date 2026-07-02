// rule_manager.c
// Rule management implementation

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "nvs.h"

#include "rule_manager.h"
#include "log_levels.h"
#include "system_config.h"
#include "nvs_config.h"
#include "sensor_read.h"
#include "relay_control.h"

// ============================================================
// Static Variables
// ============================================================
static automation_rule_t rules[MAX_RULES];
static int rule_count = 0;
static const char *nvs_namespace = "wqms";

// ============================================================
// Internal Functions
// ============================================================

// Save a single rule to NVS
static void save_rule_to_nvs(automation_rule_t *rule) {
    nvs_handle_t handle;
    if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) != ESP_OK) {
        LOG_E("Failed to open NVS for rule save");
        return;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "rule_%d", rule->rule_id);
    nvs_set_blob(handle, key, rule, sizeof(automation_rule_t));
    nvs_commit(handle);
    nvs_close(handle);
}

// Load a single rule from NVS
static int load_rule_from_nvs(uint8_t rule_id, automation_rule_t *rule) {
    nvs_handle_t handle;
    if (nvs_open(nvs_namespace, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "rule_%d", rule_id);
    size_t len = sizeof(automation_rule_t);
    int result = nvs_get_blob(handle, key, rule, &len);
    nvs_close(handle);
    
    if (result != ESP_OK) {
        return -1;
    }
    
    return 0;
}

// ============================================================
// Public Functions
// ============================================================

void rule_manager_init(void) {
    // Load rules from NVS
    rule_count = 0;
    for (int i = 0; i < MAX_RULES; i++) {
        if (load_rule_from_nvs(i, &rules[i]) == 0) {
            rule_count++;
        } else {
            // Clear the rule slot
            memset(&rules[i], 0, sizeof(automation_rule_t));
            rules[i].rule_id = -1;
        }
    }
    
    LOG_I("Rule manager initialized: %d rules loaded", rule_count);
}

automation_rule_t* rule_get_all(void) {
    return rules;
}

automation_rule_t* rule_get(uint8_t rule_id) {
    if (rule_id >= MAX_RULES) return NULL;
    return &rules[rule_id];
}

int rule_get_count(void) {
    return rule_count;
}

int rule_create(automation_rule_t *rule) {
    if (!rule) return -1;
    if (rule_count >= MAX_RULES) return -2;
    
    // Find first free slot
    int slot = -1;
    for (int i = 0; i < MAX_RULES; i++) {
//        LOG_I("Rule #%d, %s, %d", rules[i].rule_id, rules[i].name,  rules[i].name[0] == '\0');
        if (rules[i].rule_id == -1 && rules[i].name[0] == '\0') {
            slot = i;
//            LOG_I("Empty Rule Slot : #%d", slot);
            break;
        }
    }
    if (slot < 0) return -3;
    
    // Copy rule
    rules[slot] = *rule;
    rules[slot].rule_id = slot;
    rules[slot].enabled = 0;  // Disabled by default
    rules[slot].trigger_count = 0;
    rules[slot].last_triggered = 0;
    
    // Set timestamps
    rules[slot].created_timestamp = time(NULL);
    strcpy(rules[slot].created_by, "admin");
    rules[slot].last_modified_timestamp = rules[slot].created_timestamp;
    strcpy(rules[slot].last_modified_by, "admin");
    
    // Save to NVS
    save_rule_to_nvs(&rules[slot]);
    rule_count++;
    
    AUTO_LOG_I("Rule #%d '%s' created: %d conditions, %d outputs",
               slot, rules[slot].name, rules[slot].condition_count, rules[slot].output_count);
    
    return slot;
}

int rule_update(uint8_t rule_id, automation_rule_t *rule) {
    if (rule_id >= MAX_RULES) return -1;
    if (!rule) return -2;
    
    // Preserve statistics
    uint32_t old_trigger_count = rules[rule_id].trigger_count;
    uint32_t old_last_triggered = rules[rule_id].last_triggered;
    
    // Update rule
    rules[rule_id] = *rule;
    rules[rule_id].rule_id = rule_id;
    rules[rule_id].trigger_count = old_trigger_count;
    rules[rule_id].last_triggered = old_last_triggered;
    rules[rule_id].last_modified_timestamp = time(NULL);
    strcpy(rules[rule_id].last_modified_by, "admin");
    
    // Save to NVS
    save_rule_to_nvs(&rules[rule_id]);
    
    AUTO_LOG_I("Rule #%d '%s' updated", rule_id, rules[rule_id].name);
    return 0;
}

int rule_delete(uint8_t rule_id) {
    if (rule_id >= MAX_RULES) return -1;
    if (rules[rule_id].name[0] == '\0') return -2;
    
    AUTO_LOG_I("Rule #%d '%s' deleted (was triggered %lu times)",
               rule_id, rules[rule_id].name, rules[rule_id].trigger_count);
    
    // Clear rule
    nvs_handle_t handle;
    if (nvs_open(nvs_namespace, NVS_READWRITE, &handle) == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "rule_%d", rule_id);
        nvs_erase_key(handle, key);
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    memset(&rules[rule_id], 0, sizeof(automation_rule_t));
    rules[rule_id].rule_id = rule_id;
    rule_count--;
    
    return 0;
}

int rule_enable(uint8_t rule_id) {
    if (rule_id >= MAX_RULES) return -1;
    if (rules[rule_id].name[0] == '\0') return -2;
    
    // Check relay uniqueness
    for (int i = 0; i < rules[rule_id].output_count; i++) {
        if (rules[rule_id].outputs[i].type == OUTPUT_RELAY) {
            uint8_t relay_id = rules[rule_id].outputs[i].id;
            if (rule_relay_in_use(relay_id, rule_id)) {
                AUTO_LOG_W("Cannot enable Rule #%d: Relay %d already in use",
                           rule_id, relay_id);
                return -3;
            }
        }
    }
    
    rules[rule_id].enabled = 1;
    save_rule_to_nvs(&rules[rule_id]);
    
    AUTO_LOG_I("Rule #%d '%s' enabled", rule_id, rules[rule_id].name);
    return 0;
}

int rule_disable(uint8_t rule_id) {
    if (rule_id >= MAX_RULES) return -1;
    if (rules[rule_id].name[0] == '\0') return -2;
    
    rules[rule_id].enabled = 0;
    save_rule_to_nvs(&rules[rule_id]);
    
    AUTO_LOG_I("Rule #%d '%s' disabled", rule_id, rules[rule_id].name);
    return 0;
}

int rule_relay_in_use(uint8_t relay_id, uint8_t exclude_rule_id) {
    for (int i = 0; i < MAX_RULES; i++) {
        if (i == exclude_rule_id) continue;
        if (!rules[i].enabled) continue;
        if (rules[i].name[0] == '\0') continue;
        
        for (int j = 0; j < rules[i].output_count; j++) {
            if (rules[i].outputs[j].type == OUTPUT_RELAY &&
                rules[i].outputs[j].id == relay_id) {
                return 1;
            }
        }
    }
    return 0;
}

void rule_get_description(automation_rule_t *rule, char *buffer, size_t size) {
    if (!rule || !buffer) return;
    
    char temp[256];
    int pos = 0;
    
    // Build condition string
    for (int i = 0; i < rule->condition_count; i++) {
        condition_t *cond = &rule->conditions[i];
        const char *sensor_name = sensor_get_name(cond->sensor_id);
        const char *comp_str = "";
        switch (cond->comparator) {
            case COMP_GREATER: comp_str = ">"; break;
            case COMP_LESS: comp_str = "<"; break;
            case COMP_EQUAL: comp_str = "=="; break;
            case COMP_NOT_EQUAL: comp_str = "!="; break;
        }
        pos += snprintf(temp + pos, sizeof(temp) - pos, "%s %s %.2f",
                        sensor_name, comp_str, cond->threshold);
        if (i < rule->condition_count - 1) {
            pos += snprintf(temp + pos, sizeof(temp) - pos, " %s ",
                            rule->logic_type == LOGIC_AND ? "AND" :
                            rule->logic_type == LOGIC_OR ? "OR" : "NOT");
        }
    }
    
    // Build output string
    char output_str[64] = "";
    for (int i = 0; i < rule->output_count; i++) {
        if (i > 0) {
            strcat(output_str, " AND ");
        }
        if (rule->outputs[i].type == OUTPUT_RELAY) {
            relay_config_t *cfg = relay_get_config(rule->outputs[i].id);
            snprintf(output_str + strlen(output_str), sizeof(output_str) - strlen(output_str),
                     "Relay %d (%s)%s",
                     rule->outputs[i].id,
                     cfg ? cfg->name : "Unknown",
                     rule->outputs[i].custom_duration ? " (custom)" : "");
        } else if (rule->outputs[i].type == OUTPUT_EMAIL) {
            strcat(output_str, "Email");
        }
    }
    
    snprintf(buffer, size, "%s → %s", temp, output_str);
}
