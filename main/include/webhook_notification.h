// notification_webhook.h
// IFTTT Webhook-based notifications

#ifndef NOTIFICATION_WEBHOOK_H
#define NOTIFICATION_WEBHOOK_H

#include <stdint.h>
#include <stdbool.h>
#include "project_defs.h"

// ============================================================
// Webhook Configuration
// ============================================================
typedef struct {
    char url[256];          // IFTTT webhook URL
    char emails[256];   // Comma-separated email addresses
    char subject[128];      // Email subject template
    uint8_t enabled;        // 0=disabled, 1=enabled
} webhook_config_t;

// ============================================================
// Notification Types
// ============================================================
typedef enum {
    WH_TYPE_INFO = 0,
    WH_TYPE_WARNING = 1,
    WH_TYPE_ALERT = 2,
    WH_TYPE_CRITICAL = 3
} wh_notif_type_t;

// ============================================================
// Function Prototypes
// ============================================================

// Initialize webhook notification system
void webhook_notification_init(void);

// Load webhook configuration from NVS
void webhook_load_config(webhook_config_t *config);

// Save webhook configuration to NVS
void webhook_save_config(webhook_config_t *config);

// Send a webhook notification
int webhook_send_notification(wh_notif_type_t type, const char *event, const char *message);

// Send automation rule notification
int webhook_send_rule_alert(const char *rule_name, const char *rule_desc);

// Test webhook configuration
int webhook_test_notification(void);

// Check if webhook is enabled
int webhook_is_enabled(void);

// Get default webhook URL
const char* webhook_get_default_url(void);

#endif // NOTIFICATION_WEBHOOK_H