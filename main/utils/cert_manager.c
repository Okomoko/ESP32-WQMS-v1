// cert_manager.c
// Generic certificate management implementation

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cert_manager.h"
#include "nvs_config.h"  // For NVS_NAMESPACE
#include "log_levels.h"
#include "logger.h"

// ============================================================
// Private Helper Functions
// ============================================================

static const char* get_cert_key(cert_type_t type) {
    switch (type) {
        case CERT_TYPE_SUPABASE:
            return NVS_KEY_SUPABASE_CERT;
        case CERT_TYPE_EMAIL:
            return NVS_KEY_EMAIL_CERT;
        default:
            return NULL;
    }
}

static const char* get_len_key(cert_type_t type) {
    switch (type) {
        case CERT_TYPE_SUPABASE:
            return NVS_KEY_SUPABASE_CERT_LEN;
        case CERT_TYPE_EMAIL:
            return NVS_KEY_EMAIL_CERT_LEN;
        default:
            return NULL;
    }
}

static esp_err_t open_nvs_handle(nvs_handle_t *handle, nvs_open_mode_t mode) {
    return nvs_open(NVS_NAMESPACE, mode, handle);
}

// ============================================================
// Public Functions
// ============================================================

// ============================================================
// Certificate Manager Implementation
// ============================================================

esp_err_t cert_manager_save(cert_type_t type, const char *cert_pem, size_t len) {
    if (!cert_pem || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *cert_key = get_cert_key(type);
    const char *len_key = get_len_key(type);
    
    if (!cert_key || !len_key) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = open_nvs_handle(&handle, NVS_READWRITE);
    if (err != ESP_OK) {
        WQMS_LOG_E("Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save certificate blob
    err = nvs_set_blob(handle, cert_key, cert_pem, len);
    if (err != ESP_OK) {
        WQMS_LOG_E("Failed to save certificate: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    // Save certificate length as uint32_t
    uint32_t len_u32 = (uint32_t)len;
    err = nvs_set_u32(handle, len_key, len_u32);
    if (err != ESP_OK) {
        WQMS_LOG_E("Failed to save certificate length: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        const char *type_name = (type == CERT_TYPE_SUPABASE) ? "Supabase" : "Email";
        WQMS_LOG_I("%s certificate saved to NVS (%zu bytes)", type_name, len);
    }
    
    return err;
}

esp_err_t cert_manager_load(cert_type_t type, char *buffer, size_t max_len, size_t *actual_len) {
    if (!buffer || !actual_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *cert_key = get_cert_key(type);
    const char *len_key = get_len_key(type);
    
    if (!cert_key || !len_key) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = open_nvs_handle(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return err;
    }
    
    // Get certificate length as uint32_t from NVS
    uint32_t cert_len_u32 = 0;
    err = nvs_get_u32(handle, len_key, &cert_len_u32);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    size_t cert_len = (size_t)cert_len_u32;
    
    if (cert_len > max_len) {
        nvs_close(handle);
        WQMS_LOG_E("Certificate too large: %zu bytes (max: %zu)", cert_len, max_len);
        return ESP_ERR_NO_MEM;
    }
    
    // Load certificate blob - use size_t for nvs_get_blob
    // Note: nvs_get_blob expects size_t*, not uint32_t*
    size_t read_len = cert_len;
    err = nvs_get_blob(handle, cert_key, buffer, &read_len);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        *actual_len = read_len;
        buffer[read_len] = '\0';  // Null-terminate
        const char *type_name = (type == CERT_TYPE_SUPABASE) ? "Supabase" : "Email";
        WQMS_LOG_D("%s certificate loaded from NVS (%zu bytes)", type_name, read_len);
    }
    
    return err;
}

bool cert_manager_has(cert_type_t type) {
    const char *len_key = get_len_key(type);
    if (!len_key) {
        return false;
    }
    
    nvs_handle_t handle;
    if (open_nvs_handle(&handle, NVS_READONLY) != ESP_OK) {
        return false;
    }
    
    uint32_t cert_len = 0;
    esp_err_t err = nvs_get_u32(handle, len_key, &cert_len);
    nvs_close(handle);
    
    return (err == ESP_OK && cert_len > 0);
}

esp_err_t cert_manager_get_size(cert_type_t type, size_t *size) {
    if (!size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *len_key = get_len_key(type);
    if (!len_key) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = open_nvs_handle(&handle, NVS_READONLY);
    if (err != ESP_OK) {
        return err;
    }
    
    uint32_t cert_len_u32 = 0;
    err = nvs_get_u32(handle, len_key, &cert_len_u32);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        *size = (size_t)cert_len_u32;
    }
    
    return err;
}

void cert_manager_clear(cert_type_t type) {
    const char *cert_key = get_cert_key(type);
    const char *len_key = get_len_key(type);
    
    if (!cert_key || !len_key) {
        return;
    }
    
    nvs_handle_t handle;
    if (open_nvs_handle(&handle, NVS_READWRITE) != ESP_OK) {
        return;
    }
    
    nvs_erase_key(handle, cert_key);
    nvs_erase_key(handle, len_key);
    nvs_commit(handle);
    nvs_close(handle);
    
    const char *type_name = (type == CERT_TYPE_SUPABASE) ? "Supabase" : "Email";
    WQMS_LOG_I("%s certificate cleared from NVS", type_name);
}

const char* cert_manager_get_cert_key(cert_type_t type) {
    return get_cert_key(type);
}

const char* cert_manager_get_len_key(cert_type_t type) {
    return get_len_key(type);
}