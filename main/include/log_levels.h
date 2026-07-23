// log_levels.h
// Compile-time log level control - definitive source for log types

#ifndef LOG_LEVELS_H
#define LOG_LEVELS_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Log Level Definitions (Macros for conditional compilation)
// ============================================================
#define WQMS_LOG_LEVEL_NONE     0
#define WQMS_LOG_LEVEL_ERROR    1
#define WQMS_LOG_LEVEL_WARN     2
#define WQMS_LOG_LEVEL_INFO     3
#define WQMS_LOG_LEVEL_DEBUG    4
#define WQMS_LOG_LEVEL_VERBOSE  5

#define CONFIG_WQMS_LOG_LEVEL WQMS_LOG_LEVEL_DEBUG
// ============================================================
// NEW: Separate Log Levels for Different Outputs
// ============================================================

// Console output level (USB + Web Console) - Show DEBUG and above
#define WQMS_LOG_LEVEL_CONSOLE  WQMS_LOG_LEVEL_DEBUG

// File output level (SPIFFS) - Show INFO and above (NO DEBUG)
#define WQMS_LOG_LEVEL_FILE     WQMS_LOG_LEVEL_INFO

// Module-specific file log levels
// Each module type can have different file logging threshold
#define WQMS_LOG_LEVEL_FILE_SYSTEM      WQMS_LOG_LEVEL_INFO
#define WQMS_LOG_LEVEL_FILE_APPLICATION WQMS_LOG_LEVEL_INFO
#define WQMS_LOG_LEVEL_FILE_AUTOMATION  WQMS_LOG_LEVEL_DEBUG  // Automation gets DEBUG in files
#define WQMS_LOG_LEVEL_FILE_NOTIFICATION WQMS_LOG_LEVEL_INFO
#define WQMS_LOG_LEVEL_FILE_API         WQMS_LOG_LEVEL_INFO
#define WQMS_LOG_LEVEL_FILE_INTEGRATION WQMS_LOG_LEVEL_INFO
#define WQMS_LOG_LEVEL_FILE_SENSOR      WQMS_LOG_LEVEL_DEBUG  // Sensors get DEBUG in files
#define WQMS_LOG_LEVEL_FILE_RELAY       WQMS_LOG_LEVEL_INFO

// Helper: Get file log level for a specific type
#define WQMS_LOG_GET_FILE_LEVEL(type) \
    ((type) == WQMS_LOG_TYPE_SYSTEM ? WQMS_LOG_LEVEL_FILE_SYSTEM : \
     (type) == WQMS_LOG_TYPE_APPLICATION ? WQMS_LOG_LEVEL_FILE_APPLICATION : \
     (type) == WQMS_LOG_TYPE_AUTOMATION ? WQMS_LOG_LEVEL_FILE_AUTOMATION : \
     (type) == WQMS_LOG_TYPE_NOTIFICATION ? WQMS_LOG_LEVEL_FILE_NOTIFICATION : \
     (type) == WQMS_LOG_TYPE_API ? WQMS_LOG_LEVEL_FILE_API : \
     (type) == WQMS_LOG_TYPE_INTEGRATION ? WQMS_LOG_LEVEL_FILE_INTEGRATION : \
     (type) == WQMS_LOG_TYPE_SENSOR ? WQMS_LOG_LEVEL_FILE_SENSOR : \
     (type) == WQMS_LOG_TYPE_RELAY ? WQMS_LOG_LEVEL_FILE_RELAY : \
     WQMS_LOG_LEVEL_FILE)

// Helper: Check if a log level should be written to console
#define WQMS_LOG_TO_CONSOLE(level) ((level) <= WQMS_LOG_LEVEL_CONSOLE)

// Helper: Check if a log level should be written to file
#define WQMS_LOG_TO_FILE(type, level) ((level) <= WQMS_LOG_GET_FILE_LEVEL(type))

// ============================================================
// Log Type Definitions
// ============================================================
typedef enum {
    WQMS_LOG_TYPE_SYSTEM = 0,
    WQMS_LOG_TYPE_APPLICATION = 1,
    WQMS_LOG_TYPE_AUTOMATION = 2,
    WQMS_LOG_TYPE_NOTIFICATION = 3,
    WQMS_LOG_TYPE_API = 4,
    WQMS_LOG_TYPE_INTEGRATION = 5,
    WQMS_LOG_TYPE_SENSOR = 6,
    WQMS_LOG_TYPE_RELAY = 7,
    WQMS_LOG_TYPE_MAX
} wqms_log_type_t;

typedef enum {
    WQMS_LOG_LEVEL_ENUM_ERROR = 1,
    WQMS_LOG_LEVEL_ENUM_WARN = 2,
    WQMS_LOG_LEVEL_ENUM_INFO = 3,
    WQMS_LOG_LEVEL_ENUM_DEBUG = 4,
    WQMS_LOG_LEVEL_ENUM_VERBOSE = 5
} wqms_log_level_t;

// ============================================================
// Conditional Logging Macros - System Log
// ============================================================
#if CONFIG_WQMS_LOG_LEVEL >= WQMS_LOG_LEVEL_ERROR
    #define WQMS_LOG_E(format, ...)  wqms_log_write(WQMS_LOG_TYPE_SYSTEM, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define APP_LOG_E(format, ...)  wqms_log_write(WQMS_LOG_TYPE_APPLICATION, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define AUTO_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_AUTOMATION, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define NOTIFICATION_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_NOTIFICATION, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define API_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_API, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define SENSOR_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_SENSOR, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define RELAY_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_RELAY, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
    #define INTEGRATION_LOG_E(format, ...) wqms_log_write(WQMS_LOG_TYPE_INTEGRATION, WQMS_LOG_LEVEL_ENUM_ERROR, format, ##__VA_ARGS__)
#else
    #define WQMS_LOG_E(format, ...)  ((void)0)
    #define APP_LOG_E(format, ...)  ((void)0)
    #define AUTO_LOG_E(format, ...)  ((void)0)
    #define NOTIFICATION_LOG_E(format, ...)  ((void)0)
    #define API_LOG_E(format, ...)  ((void)0)
    #define SENSOR_LOG_E(format, ...)  ((void)0)
    #define RELAY_LOG_E(format, ...)  ((void)0)
    #define INTEGRATION_LOG_E(format, ...)  ((void)0)
#endif

#if CONFIG_WQMS_LOG_LEVEL >= WQMS_LOG_LEVEL_WARN
    #define WQMS_LOG_W(format, ...)  wqms_log_write(WQMS_LOG_TYPE_SYSTEM, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define APP_LOG_W(format, ...)  wqms_log_write(WQMS_LOG_TYPE_APPLICATION, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define AUTO_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_AUTOMATION, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define NOTIFICATION_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_NOTIFICATION, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define API_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_API, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define SENSOR_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_SENSOR, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define RELAY_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_RELAY, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
    #define INTEGRATION_LOG_W(format, ...) wqms_log_write(WQMS_LOG_TYPE_INTEGRATION, WQMS_LOG_LEVEL_ENUM_WARN, format, ##__VA_ARGS__)
#else
    #define WQMS_LOG_W(format, ...)  ((void)0)
    #define APP_LOG_W(format, ...)  ((void)0)
    #define AUTO_LOG_W(format, ...)  ((void)0)
    #define NOTIFICATION_LOG_W(format, ...)  ((void)0)
    #define API_LOG_W(format, ...)  ((void)0)
    #define SENSOR_LOG_W(format, ...)  ((void)0)
    #define RELAY_LOG_W(format, ...)  ((void)0)
    #define INTEGRATION_LOG_W(format, ...)  ((void)0)
#endif

#if CONFIG_WQMS_LOG_LEVEL >= WQMS_LOG_LEVEL_INFO
    #define WQMS_LOG_I(format, ...)  wqms_log_write(WQMS_LOG_TYPE_SYSTEM, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define APP_LOG_I(format, ...)  wqms_log_write(WQMS_LOG_TYPE_APPLICATION, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define AUTO_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_AUTOMATION, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define NOTIFICATION_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_NOTIFICATION, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define API_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_API, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define SENSOR_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_SENSOR, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define RELAY_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_RELAY, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
    #define INTEGRATION_LOG_I(format, ...) wqms_log_write(WQMS_LOG_TYPE_INTEGRATION, WQMS_LOG_LEVEL_ENUM_INFO, format, ##__VA_ARGS__)
#else
    #define WQMS_LOG_I(format, ...)  ((void)0)
    #define APP_LOG_I(format, ...)  ((void)0)
    #define AUTO_LOG_I(format, ...)  ((void)0)
    #define NOTIFICATION_LOG_I(format, ...)  ((void)0)
    #define API_LOG_I(format, ...)  ((void)0)
    #define SENSOR_LOG_I(format, ...)  ((void)0)
    #define RELAY_LOG_I(format, ...)  ((void)0)
    #define INTEGRATION_LOG_I(format, ...)  ((void)0)
#endif

#if CONFIG_WQMS_LOG_LEVEL >= WQMS_LOG_LEVEL_DEBUG
    #define WQMS_LOG_D(format, ...)  wqms_log_write(WQMS_LOG_TYPE_SYSTEM, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define APP_LOG_D(format, ...)  wqms_log_write(WQMS_LOG_TYPE_APPLICATION, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define AUTO_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_AUTOMATION, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define NOTIFICATION_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_NOTIFICATION, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define API_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_API, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define SENSOR_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_SENSOR, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define RELAY_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_RELAY, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
    #define INTEGRATION_LOG_D(format, ...) wqms_log_write(WQMS_LOG_TYPE_INTEGRATION, WQMS_LOG_LEVEL_ENUM_DEBUG, format, ##__VA_ARGS__)
#else
    #define WQMS_LOG_D(format, ...)  ((void)0)
    #define APP_LOG_D(format, ...)  ((void)0)
    #define AUTO_LOG_D(format, ...)  ((void)0)
    #define NOTIFICATION_LOG_D(format, ...)  ((void)0)
    #define API_LOG_D(format, ...)  ((void)0)
    #define SENSOR_LOG_D(format, ...)  ((void)0)
    #define RELAY_LOG_D(format, ...)  ((void)0)
    #define INTEGRATION_LOG_D(format, ...)  ((void)0)
#endif

#if CONFIG_WQMS_LOG_LEVEL >= WQMS_LOG_LEVEL_VERBOSE
    #define WQMS_LOG_V(format, ...)  wqms_log_write(WQMS_LOG_TYPE_SYSTEM, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define APP_LOG_V(format, ...)  wqms_log_write(WQMS_LOG_TYPE_APPLICATION, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define AUTO_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_AUTOMATION, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define NOTIFICATION_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_NOTIFICATION, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define API_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_API, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define SENSOR_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_SENSOR, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define RELAY_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_RELAY, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
    #define INTEGRATION_LOG_V(format, ...) wqms_log_write(WQMS_LOG_TYPE_INTEGRATION, WQMS_LOG_LEVEL_ENUM_VERBOSE, format, ##__VA_ARGS__)
#else
    #define WQMS_LOG_V(format, ...)  ((void)0)
    #define APP_LOG_V(format, ...)  ((void)0)
    #define AUTO_LOG_V(format, ...)  ((void)0)
    #define NOTIFICATION_LOG_V(format, ...)  ((void)0)
    #define API_LOG_V(format, ...)  ((void)0)
    #define SENSOR_LOG_V(format, ...)  ((void)0)
    #define RELAY_LOG_V(format, ...)  ((void)0)
    #define INTEGRATION_LOG_V(format, ...)  ((void)0)
#endif

// ============================================================
// RS232 Console Output
// ============================================================
#ifdef CONFIG_RS232_CONSOLE_ENABLE
    #define WQMS_RS232_PRINT(format, ...) \
        uart_write_bytes(UART_NUM_0, format, strlen(format))
#else
    #define WQMS_RS232_PRINT(format, ...) ((void)0)
#endif

// ============================================================
// Function Prototypes
// ============================================================
void wqms_log_write(wqms_log_type_t type, wqms_log_level_t level, const char *format, ...);

// NEW: Get string representation of log type
const char* wqms_log_type_to_string(wqms_log_type_t type);

#endif // LOG_LEVELS_H