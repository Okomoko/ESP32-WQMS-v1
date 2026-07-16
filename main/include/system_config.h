// system_config.h
// Centralized system-wide configuration constants

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdint.h>
#include <stddef.h>

// ============================================================
// Hardware Configuration
// ============================================================
#define ANALOGUE_SENSOR_COUNT  6
#define DIGITAL_SENSOR_COUNT   2
#define TOTAL_SENSOR_COUNT     (ANALOGUE_SENSOR_COUNT + DIGITAL_SENSOR_COUNT)
#define RELAY_COUNT            10
#define MODBUS_MAP_ENTRY_COUNT (RELAY_COUNT + TOTAL_SENSOR_COUNT)
#define ADC_CHANNEL_COUNT      8
// DHT11 sensor IDs - zero indexed id, always use the last two sensors (if there are 10 sensors, numbers must be 8 and 9 (starting from zero))
#define DHT11_SENSOR_TEMP      (ANALOGUE_SENSOR_COUNT)
#define DHT11_SENSOR_HUMID     (ANALOGUE_SENSOR_COUNT + 1)

extern const char* default_sensor_names[];
/*

Pin NamePin #    GPIO    Type    Primary Capabilities & Notes
3V3      1         -    Power    3.3V Power Supply Output
EN       2         -    Input    CHIP_PU / Reset. Pull LOW to reset.
SVP      3      GPIO36    Input    ADC1_CH0 (Analog/Sensor VP)
SVN      4      GPIO39    Input    ADC1_CH3 (Analog/Sensor VN)
IO34     5      GPIO34    Input    ADC1_CH6 (Input-only pin)
IO35     6      GPIO35    Input    ADC1_CH7 (Input-only pin)
IO32     7      GPIO32    I/O     ADC1_CH4, TOUCH_9, RTC_32K_P
IO33     8      GPIO33    I/O     ADC1_CH5, TOUCH_8, RTC_32K_N
IO25     9      GPIO25    I/O     ADC2_CH8, DAC1
IO26    10      GPIO26    I/O     ADC2_CH9, DAC2
IO27    11      GPIO27    I/O     ADC2_CH7, TOUCH_7
IO14    12      GPIO14    I/O     ADC2_CH6, TOUCH_6, HSPI_CLK
IO12    13      GPIO12    I/O     ADC2_CH5, TOUCH_5, HSPI_MISO
GND     14         -    Power    Ground
IO13    15      GPIO13    I/O     ADC2_CH4, TOUCH_4, HSPI_MOSI
IO9     16      GPIO9    I/O     Connected to Flash (Avoid using)
IO10    17      GPIO10    I/O     Connected to Flash (Avoid using)
IO11    18      GPIO11    I/O     Connected to Flash (Avoid using)
VIN     19         -    Power    5V Power Supply Input (via USB)
GND     20         -    Power    Ground
IO6     21      GPIO6    I/O     Connected to Flash (Avoid using)
IO7     22      GPIO7    I/O     Connected to Flash (Avoid using)
IO8     23      GPIO8    I/O     Connected to Flash (Avoid using)
IO15    24      GPIO15    I/O     ADC2_CH3, TOUCH_3. Boot Strapping (Needs HIGH)
IO2     25      GPIO2    I/O     ADC2_CH2, TOUCH_2. Boot Strapping Pin
IO0     26      GPIO0    I/O     ADC2_CH1, TOUCH_1. Pull LOW for Boot Mode
IO4     27      GPIO4    I/O     ADC2_CH0, TOUCH_0
IO16    28      GPIO16    I/O     UART2 RX
IO17    29      GPIO17    I/O     UART2 TX
IO5     30      GPIO5    I/O     VSPI_CS0 / Boot Strapping (Needs HIGH)
IO18    31      GPIO18    I/O     VSPI_CLK
IO19    32      GPIO19    I/O     VSPI_MISO
IO21    33      GPIO21    I/O     I2C SDA
IO3     34      GPIO3    I/O     U0RXD (Default Serial Input/RX)
IO1     35      GPIO1    I/O     U0TXD (Default Serial Output/TX)
IO22    36      GPIO22    I/O     I2C SCL
IO23    37      GPIO23    I/O     VSPI_MOSI
GND     38         -    Power    Ground

1.    Input-Only Pins: GPIO34, GPIO35, GPIO36 (VP), and GPIO39 (VN) cannot output voltage or act as digital outputs.
2.    Flash Memory Pins: GPIO6 through GPIO11 are strictly connected to the onboard SPI flash memory. Using them for other tasks will cause your ESP32 to fail or crash.
3.    Strapping Pins: GPIO 0, GPIO 2, GPIO 5, GPIO 12, and GPIO 15 determine the boot mode. Avoid pulling these pins High/Low externally during startup to prevent boot failure.
4.    Logic Level: The ESP32 is a 3.3V device. Connecting 5V signals directly to the pins will permanently damage the microcontroller.

*/

//check pin configuration in nvs_config.c

// Digital temperature and humidity sensor connection
#define GPIO_DHT11             13 //Digital sensor I/O pin

// ============================================================
// Relay Configuration
// ============================================================
#define RELAY_MIN_DURATION_MS     50      // Minimum 50ms (safe for mechanical)
#define RELAY_MAX_DURATION_MS     60000   // Maximum 60 seconds
#define RELAY_DEFAULT_DURATION_MS 100     // Default 100ms (safe for all)
#define RELAY_DEFAULT_OFFDELAY_MS 5000    // Default 5000ms to allow re-trigger the same relay

// ============================================================
// Sensor History Configuration (32 days fixed)
// ============================================================
#define HISTORY_DAYS                  32
#define HISTORY_SAMPLES_PER_DAY       1440
#define HISTORY_MAX_RECORDS           (HISTORY_DAYS * HISTORY_SAMPLES_PER_DAY)
#define HISTORY_MAX_RECORDS_PER_PAGE  (3* HISTORY_SAMPLES_PER_DAY)
#define SENSOR_RECORD_SIZE            26
#define HISTORY_FILE_SIZE             (HISTORY_MAX_RECORDS * SENSOR_RECORD_SIZE)
#define HISTORY_FILE                  "/spiffs/sensors/history.dat"
#define INDEX_FILE                    "/spiffs/sensors/index.dat"

// ============================================================
// Logging Configuration
// ============================================================
#define LOG_MAX_FILES                      2
#define LOG_FILE_SIZE                 524288
#define CONFIG_RS232_CONSOLE_ENABLE        0

// ============================================================
// Automation Configuration
// ============================================================
#define MAX_RULES                         32
#define MAX_CONDITIONS_PER_RULE            8
#define MAX_OUTPUTS_PER_RULE               4
#define RULE_NAME_MAX_LEN                 32
#define DEFAULT_AUTOMATION_INTERVAL_SEC   60

// ============================================================
// MODBUS Configuration
// ============================================================
#define MODBUS_REGISTER_COUNT  1024
#define MODBUS_TCP_PORT        502
#define MODBUS_HEARTBEAT_REG   0x0300
#define MODBUS_COMMAND_REG     0x0310
#define MODBUS_SENSOR_BASE     0x0000
#define MODBUS_RELAY_BASE      0x0010
#define MODBUS_CALIB_BASE      0x0100
#define MODBUS_DURATION_BASE   0x0110
#define MODBUS_OFFDELAY_BASE   0x0120
#define MODBUS_SYSTEM_BASE     0x0200
#define MODBUS_STATUS_BASE     0x0300

// ============================================================
// WiFi Configuration
// ============================================================
#define WIFI_AP_SSID_PREFIX        "WQMS-"
#define WIFI_AP_PASSWORD           "WQMSwqms"
#define WIFI_MAX_RETRIES           5
#define WIFI_RETRY_DELAY_MS        2000
#define WIFI_SCAN_MAX_NETWORKS     20

// ============================================================
// NTP Configuration
// ============================================================
#define NTP_FAST_SYNC_INTERVAL_MS    2000
#define NTP_FAST_SYNC_MAX_ATTEMPTS   10
#define NTP_NORMAL_SYNC_INTERVAL_SEC 3600
#define NTP_BACKOFF_BASE_SEC         60
#define NTP_BACKOFF_MAX_SEC          3600
#define NTP_DEFAULT_TIMEZONE         "EET-3"

// ============================================================
// Integration Configuration
// ============================================================
#define INTEGRATION_BUFFER_MAX_ITEMS     100
#define INTEGRATION_WEB_TIMEOUT_MS       5000
#define DEFAULT_INTEGRATION_INTERVAL_SEC 60

// ============================================================
// Task Stack Sizes
// ============================================================
#define STACK_SIZE_MAIN          8192
#define STACK_SIZE_SENSOR        4096
#define STACK_SIZE_RELAY         1024
#define STACK_SIZE_MODBUS        4096
#define STACK_SIZE_WIFI          3072
#define STACK_SIZE_NTP           4096
#define STACK_SIZE_WATCHDOG      1024
#define STACK_SIZE_WEBSERVER    16384
#define STACK_SIZE_AUTOMATION   16384
#define STACK_SIZE_INTEGRATION   2048
#define STACK_SIZE_LOGGING       1024

// ============================================================
// Task Priorities
// ============================================================
#define PRIORITY_SENSOR         5
#define PRIORITY_RELAY          4
#define PRIORITY_MODBUS         3
#define PRIORITY_WIFI           2
#define PRIORITY_NTP            2
#define PRIORITY_WATCHDOG       1
#define PRIORITY_WEBSERVER      1
#define PRIORITY_AUTOMATION     3
#define PRIORITY_INTEGRATION    2
#define PRIORITY_LOGGING        1

#define DEFAULT_RECIPIENTS "okan.sengun@gmail.com"
#define DEFAULT_SUBJECT "WQMS System Alert - {{system_name}}"

#endif // SYSTEM_CONFIG_H