// log_levels.h
// Compile-time log level control - definitive source for log types

#ifndef LOG_LEVELS_H
#define LOG_LEVELS_H

#include "sdkconfig.h"

// ============================================================
// Log Level Definitions (Macros for conditional compilation)
// ============================================================
#define WQMS_LOG_LEVEL_NONE     0
#define WQMS_LOG_LEVEL_ERROR    1
#define WQMS_LOG_LEVEL_WARN     2
#define WQMS_LOG_LEVEL_INFO     3
#define WQMS_LOG_LEVEL_DEBUG    4
#define WQMS_LOG_LEVEL_VERBOSE  5

// Default to INFO if not defined
#ifndef CONFIG_WQMS_LOG_LEVEL
    #define CONFIG_WQMS_LOG_LEVEL WQMS_LOG_LEVEL_INFO
#endif

// ============================================================
// Log Type Definitions (Enum values use different naming)
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

#endif // LOG_LEVELS_H