// rule_manager.h
// Rule management - definitions and public API
// ESP-IDF v6.0.1

#ifndef RULE_MANAGER_H
#define RULE_MANAGER_H

#include <stdint.h>
#include <time.h>
#include "project_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Public API Functions
// ============================================================

/**
 * @brief Initialize the rule manager
 * @note Loads all rules from NVS storage
 */
void rule_manager_init(void);

/**
 * @brief Get all rules
 * @return Pointer to array of rules (MAX_RULES entries)
 */
automation_rule_t* rule_get_all(void);

/**
 * @brief Get a single rule by ID
 * @param rule_id Rule ID (0..MAX_RULES-1)
 * @return Pointer to rule, or NULL if not found
 */
automation_rule_t* rule_get(uint8_t rule_id);

/**
 * @brief Get the number of active rules
 * @return Number of rules currently stored
 */
int rule_get_count(void);

/**
 * @brief Create a new rule
 * @param rule Pointer to rule data (ID will be auto-assigned)
 * @return Rule ID (0..MAX_RULES-1) on success, negative on error
 */
int rule_create(automation_rule_t *rule);

/**
 * @brief Update an existing rule
 * @param rule_id Rule ID to update
 * @param rule Pointer to new rule data
 * @return 0 on success, negative on error
 */
int rule_update(uint8_t rule_id, automation_rule_t *rule);

/**
 * @brief Delete a rule
 * @param rule_id Rule ID to delete
 * @return 0 on success, negative on error
 */
int rule_delete(uint8_t rule_id);

/**
 * @brief Enable a rule
 * @param rule_id Rule ID to enable
 * @return 0 on success, negative on error
 */
int rule_enable(uint8_t rule_id);

/**
 * @brief Disable a rule
 * @param rule_id Rule ID to disable
 * @return 0 on success, negative on error
 */
int rule_disable(uint8_t rule_id);

/**
 * @brief Check if a relay is already used by any enabled rule
 * @param relay_id Relay ID to check
 * @param exclude_rule_id Rule ID to exclude from check
 * @return 1 if relay is in use, 0 otherwise
 */
int rule_relay_in_use(uint8_t relay_id, uint8_t exclude_rule_id);

/**
 * @brief Get human-readable description of a rule
 * @param rule Pointer to rule
 * @param buffer Output buffer
 * @param size Buffer size
 */
void rule_get_description(automation_rule_t *rule, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // RULE_MANAGER_H