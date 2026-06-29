// web_upload.h
// 3rd party web service integration

#ifndef WEB_UPLOAD_H
#define WEB_UPLOAD_H

#include <stdint.h>

// ============================================================
// Function Prototypes
// ============================================================

// Initialize web upload subsystem
void web_upload_init(void);

// Upload current sensor readings
int web_upload_sensors(void);

// Get upload status
int web_upload_get_status(void);

// Get last upload result message
const char* web_upload_get_last_result(void);

#endif // WEB_UPLOAD_H