// wifi_manager.h
// WiFi manager - STA mode with AP fallback

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_wifi.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize WiFi subsystem
void wifi_init(void);

// Get current WiFi mode (0=AP, 1=STA, 2=AP+STA)
int wifi_get_mode(void);

// Get current SSID (AP or STA)
const char* wifi_get_ssid(void);

// Get AP SSID (when in AP mode)
const char* wifi_get_ap_ssid(void);

// Get AP password
const char* wifi_get_ap_password(void);

// Get IP address string
const char* wifi_get_ip(void);

// Get MAC address string
const char* wifi_get_mac(void);

// Connect to STA network (with credentials)
int wifi_connect_sta(const char *ssid, const char *password);

// Disconnect from STA network
void wifi_disconnect_sta(void);

// Forget current STA network (remove from NVS)
void wifi_forget_network(void);

// Scan for available networks
int wifi_scan_networks(void);

// Get scan results (call after wifi_scan_networks)
int wifi_get_scan_results(wifi_ap_record_t **records, int *count);

// Check if WiFi is connected
int wifi_is_connected(void);

// Get signal strength (RSSI)
int wifi_get_rssi(void);

// Set WiFi mode: 0=AP, 1=STA, 2=AP+STA
int wifi_set_mode(int mode);

// Get WiFi mode as string
const char* wifi_get_mode_string(void);

#endif // WIFI_MANAGER_H