#ifndef EMAIL_CLIENT_H
#define EMAIL_CLIENT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Email configuration structure
typedef struct {
    char smtp_server[64];      // e.g., "smtp.gmail.com"
    uint16_t smtp_port;        // 465 (SSL) or 587 (TLS)
    char username[64];         // e.g., "your-email@gmail.com"
    char password[64];         // App password or regular password
    char from_email[64];       // Sender address
    char to_emails[256];       // Comma-separated list of recipients
    bool use_tls;              // true for TLS, false for SSL
    bool enabled;              // Master enable/disable
} email_config_t;

// Email message structure
typedef struct {
    char subject[128];
    char body[1024];
    char *attachments;          // Not implemented yet, placeholder
} email_message_t;

/**
 * @brief Initialize email client module
 * @return ESP_OK on success
 */
esp_err_t email_client_init(void);

/**
 * @brief Load email configuration from NVS
 * @param config Pointer to config structure to fill
 * @return ESP_OK on success
 */
esp_err_t email_load_config(email_config_t *config);

/**
 * @brief Save email configuration to NVS
 * @param config Pointer to config structure to save
 * @return ESP_OK on success
 */
esp_err_t email_save_config(const email_config_t *config);

/**
 * @brief Send an email
 * @param message Pointer to email message structure
 * @return ESP_OK on success
 */
esp_err_t email_send(const email_message_t *message);

/**
 * @brief Send email notification with system state
 * @param subject Email subject
 * @param body Email body (can include system info)
 * @return ESP_OK on success
 */
esp_err_t email_send_notification(const char *subject, const char *body);

/**
 * @brief Test email configuration by sending a test email
 * @return ESP_OK on success
 */
esp_err_t email_send_test(void);

/**
 * @brief Check if email is configured and enabled
 * @return true if configured and enabled
 */
bool email_is_configured(void);

#ifdef __cplusplus
}
#endif

#endif // EMAIL_CLIENT_H