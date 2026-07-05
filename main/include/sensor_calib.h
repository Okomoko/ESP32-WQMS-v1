#ifndef SENSOR_CALIB_H
#define SENSOR_CALIB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Constants
// ============================================================
#define CAL_SAMPLES_MAX 10          // Maximum samples per calibration
#define CAL_MIN_SAMPLES 2            // Minimum samples needed to apply

// ============================================================
// Data Structures
// ============================================================

/**
 * @brief A single calibration sample
 */
typedef struct {
    float raw_adc;          // Raw ADC value read from sensor
    float voltage;          // Voltage reading (if applicable)
    float known_value;      // Known standard value (e.g., pH 7.00)
} cal_sample_t;

/**
 * @brief Calibration session state
 */
typedef struct {
    int sensor_id;                      // Which sensor is being calibrated
    int sample_count;                   // Number of samples collected
    cal_sample_t samples[CAL_SAMPLES_MAX];  // Collected samples
    uint32_t start_time;                // Session start timestamp (ms)
    bool active;                        // Is session active?
} cal_session_t;

// ============================================================
// Function Prototypes
// ============================================================

/**
 * @brief Start a calibration session for a sensor
 * @param sensor_id Sensor ID to calibrate (0 to TOTAL_SENSOR_COUNT-1)
 * @return 0 on success, -1 on error (invalid sensor or already active)
 */
int cal_start(int sensor_id);

/**
 * @brief Add a sample to the current calibration session
 * @param known_value The known value of the calibration standard
 * @return 0 on success, -1 on error (no active session or session full)
 */
int cal_add_sample(float known_value);

/**
 * @brief Calculate the calibration factor from collected samples
 * @return Calibration factor, or 0 if not enough samples
 */
float cal_calculate_factor(void);

/**
 * @brief Apply the calibration and save to NVS
 * @return 0 on success, -1 on error (not enough samples or save failed)
 */
int cal_apply(void);

/**
 * @brief Cancel the current calibration session
 * @return 0 on success, -1 on error (no active session)
 */
int cal_cancel(void);

/**
 * @brief Get the current calibration session
 * @return Pointer to session, or NULL if no active session
 */
cal_session_t* cal_get_session(void);

/**
 * @brief Save calibration factor to NVS
 * @param sensor_id Sensor ID
 * @param factor Calibration factor to save
 * @return 0 on success, -1 on error
 */
int cal_save_factor(int sensor_id, float factor);

/**
 * @brief Load calibration factor from NVS
 * @param sensor_id Sensor ID
 * @param factor Pointer to store the loaded factor
 * @return 0 on success, -1 on error (not found or read failed)
 */
int cal_load_factor(int sensor_id, float *factor);

/**
 * @brief Check if a calibration session is active
 * @return true if active, false otherwise
 */
bool cal_is_active(void);

/**
 * @brief Get the number of samples in current session
 * @return Number of samples, or 0 if no active session
 */
int cal_get_sample_count(void);

/**
 * @brief Reset the calibration module (clear any state)
 */
void cal_reset(void);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_CALIB_H