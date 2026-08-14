#ifndef SUPABASE_UPLOAD_H
#define SUPABASE_UPLOAD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Initialization & Control
// ============================================================

/**
 * @brief Initialize the Supabase upload system
 * @return ESP_OK on success
 */
esp_err_t supabase_upload_init(void);

/**
 * @brief Start the upload task
 * @return ESP_OK on success
 */
esp_err_t supabase_upload_start(void);

/**
 * @brief Stop the upload task
 * @return ESP_OK on success
 */
esp_err_t supabase_upload_stop(void);

// ============================================================
// Upload Functions (Can be called manually)
// ============================================================

/**
 * @brief Upload pending sensor data from history
 * @return Number of records uploaded, or negative on error
 */
int supabase_upload_pending_sensors(void);

/**
 * @brief Upload pending logs from circular buffer
 * @return Number of logs uploaded, or negative on error
 */
int supabase_upload_pending_logs(void);

// ============================================================
// Status & Information
// ============================================================

/**
 * @brief Get upload status
 * @param sensor_ok Pointer to store sensor upload status
 * @param log_ok Pointer to store log upload status
 */
void supabase_get_status(bool *sensor_ok, bool *log_ok);

/**
 * @brief Get last upload time for sensor
 * @return Timestamp of last successful sensor upload
 */
uint64_t supabase_get_last_sensor_upload(void);

/**
 * @brief Get last upload time for logs
 * @return Timestamp of last successful log upload
 */
uint64_t supabase_get_last_log_upload(void);

/**
 * @brief Sync current sensor configuration to Supabase
 * This sends the current sensor names/units to the sensor_config table
 * Should be called whenever sensor configuration changes
 * @return ESP_OK on success
 */
esp_err_t supabase_sync_sensor_config(void);

#ifdef __cplusplus
}
#endif

#endif // SUPABASE_UPLOAD_H