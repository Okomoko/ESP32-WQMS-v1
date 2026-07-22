// sensor_history.h
// 32-day ring buffer for sensor history

#ifndef SENSOR_HISTORY_H
#define SENSOR_HISTORY_H

#include <stdint.h>
#include "project_defs.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize history storage
void sensor_history_init(void);

// Add current readings to history (called once per minute)
void sensor_history_add(void);

// Get history records based on record numbers
int sensor_history_get_records(uint32_t starting_offset, sensor_record_t *buffer, uint32_t number_of_records);

// Get history records for a time range
int sensor_history_get_range(uint32_t start_ts, uint32_t end_ts, 
                             sensor_record_t *buffer, int max_records);

// Get the latest record
int sensor_history_get_latest(sensor_record_t *record);

// Get the oldest record timestamp
uint32_t sensor_history_get_oldest_ts(void);

// Get the newest record timestamp
uint32_t sensor_history_get_newest_ts(void);

// Get total number of records stored
uint32_t sensor_history_get_record_count(void);

// Export history to CSV (callback for web server)
int sensor_history_export_csv(uint32_t start_ts, uint32_t end_ts, 
                              void (*write_callback)(const char *));

#endif // SENSOR_HISTORY_H