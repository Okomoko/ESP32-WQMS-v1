// buffer_recovery.h
// Upload buffer for failed transmissions

#ifndef BUFFER_RECOVERY_H
#define BUFFER_RECOVERY_H

#include <stdint.h>

// ============================================================
// Function Prototypes
// ============================================================

// Initialize the recovery buffer
void buffer_recovery_init(void);

// Add a failed upload to the buffer
int buffer_recovery_add_pending(const char *data);

// Retry all pending uploads
int buffer_recovery_retry_all(void);

// Clear all pending uploads
void buffer_recovery_clear_pending(void);

// Get pending count
int buffer_recovery_get_pending_count(void);

// Get buffer status
const char* buffer_recovery_get_status(void);

#endif // BUFFER_RECOVERY_H