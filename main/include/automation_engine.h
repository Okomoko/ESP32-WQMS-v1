// automation_engine.h
// Rule evaluation engine

#ifndef AUTOMATION_ENGINE_H
#define AUTOMATION_ENGINE_H

#include "project_defs.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize automation engine
void automation_init(void);

// Evaluate all enabled rules
void automation_evaluate(void);

// Force evaluation of a specific rule (for testing)
int automation_test_rule(uint8_t rule_id);

// Get the last evaluation time
uint32_t automation_get_last_eval_time(void);

#endif // AUTOMATION_ENGINE_H