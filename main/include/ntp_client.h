// ntp_client.h
// NTP client with aggressive boot sync and NVS storage

#ifndef NTP_CLIENT_H
#define NTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Function Prototypes
// ============================================================

// Start NTP client task
void ntp_start(void);

// Force an immediate NTP sync
int ntp_force_sync(void);

// Get NTP sync status
int ntp_is_synced(void);

// Get timezone string
const char* ntp_get_timezone(void);

// Set timezone
void ntp_set_timezone(const char *tz);

// Get NTP server list (comma-separated)
const char* ntp_get_servers(void);

// Set NTP server list (saves to NVS and restarts NTP)
void ntp_set_servers(const char *servers);

// Set manual time (Unix timestamp)
void ntp_set_manual_time(uint32_t timestamp);

// Get current system time (Unix timestamp)
uint32_t ntp_get_time(void);

#endif // NTP_CLIENT_H