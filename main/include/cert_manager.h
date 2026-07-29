// cert_manager.h
// Generic certificate management for NVS storage

#ifndef CERT_MANAGER_H
#define CERT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Certificate Types
// ============================================================
typedef enum {
    CERT_TYPE_SUPABASE = 0,
    CERT_TYPE_EMAIL    = 1,
    CERT_TYPE_MAX
} cert_type_t;

// ============================================================
// Function Prototypes
// ============================================================

/**
 * @brief Save certificate to NVS
 * @param type Certificate type (CERT_TYPE_SUPABASE or CERT_TYPE_EMAIL)
 * @param cert_pem Certificate string (PEM format)
 * @param len Length of certificate string
 * @return ESP_OK on success
 */
esp_err_t cert_manager_save(cert_type_t type, const char *cert_pem, size_t len);

/**
 * @brief Load certificate from NVS
 * @param type Certificate type
 * @param buffer Buffer to store certificate
 * @param max_len Maximum buffer size
 * @param actual_len Pointer to store actual certificate length
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no certificate
 */
esp_err_t cert_manager_load(cert_type_t type, char *buffer, size_t max_len, size_t *actual_len);

/**
 * @brief Check if certificate exists in NVS
 * @param type Certificate type
 * @return true if certificate exists
 */
bool cert_manager_has(cert_type_t type);

/**
 * @brief Get certificate size from NVS
 * @param type Certificate type
 * @param size Pointer to store size
 * @return ESP_OK on success
 */
esp_err_t cert_manager_get_size(cert_type_t type, size_t *size);

/**
 * @brief Clear certificate from NVS
 * @param type Certificate type
 */
void cert_manager_clear(cert_type_t type);

/**
 * @brief Get certificate NVS key string
 * @param type Certificate type
 * @return NVS key string
 */
const char* cert_manager_get_cert_key(cert_type_t type);

/**
 * @brief Get certificate length NVS key string
 * @param type Certificate type
 * @return NVS key string
 */
const char* cert_manager_get_len_key(cert_type_t type);

#ifdef __cplusplus
}
#endif

#endif // CERT_MANAGER_H