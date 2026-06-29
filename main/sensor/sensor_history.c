// sensor_history.c
// 32-day ring buffer implementation

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "sensor_history.h"
#include "sensor_read.h"
#include "log_levels.h"
#include "system_config.h"
#include "nvs_config.h"

// ============================================================
// Constants
// ============================================================
#define HISTORY_FILE "/spiffs/sensors/history.dat"
#define INDEX_FILE "/spiffs/sensors/index.dat"

// ============================================================
// Data Structures
// ============================================================
typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0x53454E53 ("SENS")
    uint32_t version;         // 1
    uint32_t max_records;     // 46,080 (32 days)
    uint32_t write_offset;    // Current write position (bytes)
    uint32_t record_count;    // Total records written
    uint32_t first_timestamp; // Oldest record timestamp
    uint32_t last_timestamp;  // Newest record timestamp
    uint16_t sensor_count;    // 10
    uint16_t sample_interval; // 60 seconds
    uint8_t reserved[94];     // Padding to 128 bytes
} history_index_t;

// ============================================================
// Static Variables
// ============================================================
static history_index_t sensor_idx = {0};
static int history_initialized = 0;

// ============================================================
// Internal Functions
// ============================================================

static int history_read_index(void) {
    FILE *f = fopen(INDEX_FILE, "rb");
    if (!f) {
        // File doesn't exist - create defaults
        sensor_idx .magic = 0x53454E53;
        sensor_idx .version = 1;
        sensor_idx .max_records = HISTORY_MAX_RECORDS;
        sensor_idx .write_offset = 0;
        sensor_idx .record_count = 0;
        sensor_idx .first_timestamp = 0;
        sensor_idx .last_timestamp = 0;
        sensor_idx .sensor_count = SENSOR_COUNT;
        sensor_idx .sample_interval = 60;
        memset(sensor_idx .reserved, 0, sizeof(sensor_idx .reserved));
        return 0;
    }
    
    size_t read = fread(&sensor_idx , 1, sizeof(sensor_idx ), f);
    fclose(f);
    
    if (read != sizeof(sensor_idx ) || sensor_idx .magic != 0x53454E53) {
        LOG_W("Invalid index file, recreating");
        // Reset to defaults
        sensor_idx .magic = 0x53454E53;
        sensor_idx .version = 1;
        sensor_idx .max_records = HISTORY_MAX_RECORDS;
        sensor_idx .write_offset = 0;
        sensor_idx .record_count = 0;
        sensor_idx .first_timestamp = 0;
        sensor_idx .last_timestamp = 0;
        sensor_idx .sensor_count = SENSOR_COUNT;
        sensor_idx .sample_interval = 60;
        return 0;
    }
    
    LOG_D("History index loaded: %lu records, newest=%lu",
          sensor_idx .record_count, sensor_idx .last_timestamp);
    return 0;
}

static int history_write_sensor_idx (void) {
    FILE *f = fopen(INDEX_FILE, "wb");
    if (!f) {
        LOG_E("Failed to open index file for writing");
        return -1;
    }
    
    size_t written = fwrite(&sensor_idx , 1, sizeof(sensor_idx ), f);
    fclose(f);
    
    if (written != sizeof(sensor_idx )) {
        LOG_E("Failed to write index file");
        return -1;
    }
    return 0;
}

static int history_file_exists(void) {
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int history_create_file(void) {
    FILE *f = fopen(HISTORY_FILE, "wb");
    if (!f) {
        LOG_E("Failed to create history file");
        return -1;
    }
    
    // Pre-allocate the full file size
    fseek(f, HISTORY_FILE_SIZE - 1, SEEK_SET);
    fputc(0, f);
    fclose(f);
    
    LOG_I("History file created: %lu bytes", HISTORY_FILE_SIZE);
    return 0;
}

// ============================================================
// Public Functions
// ============================================================

void sensor_history_init(void) {
    // Read index
    history_read_index();
    
    // Create history file if it doesn't exist
    if (!history_file_exists()) {
        if (history_create_file() != 0) {
            LOG_E("Failed to create history file");
            return;
        }
    }
    
    history_initialized = 1;
    LOG_I("Sensor history initialized: %lu records (%lu max)",
          sensor_idx .record_count, sensor_idx .max_records);
}

void sensor_history_add(void) {
    if (!history_initialized) return;
    
    // 1. Get current readings
    sensor_reading_t *readings = sensor_get_all_readings();
    if (!readings) return;
    
    // 2. Build record
    sensor_record_t record = {0};
    record.timestamp = time(NULL);
    record.sensor_mask = 0;
    
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (readings[i].status == SENSOR_STATUS_OK) {
            record.sensor_mask |= (1 << i);
            // Store as scaled integer (pH×100, temp×10, etc.)
            if (i == SENSOR_PH) {
                record.values[i] = (uint16_t)(readings[i].value * 100);
            } else if (i == SENSOR_TEMPERATURE) {
                record.values[i] = (uint16_t)(readings[i].value * 10);
            } else {
                record.values[i] = (uint16_t)readings[i].value;
            }
        } else {
            record.values[i] = 0;
        }
    }
    
    // 3. Write to file at current offset
    FILE *f = fopen(HISTORY_FILE, "r+b");
    if (!f) {
        LOG_E("Failed to open history file for writing");
        return;
    }
    
    fseek(f, sensor_idx .write_offset, SEEK_SET);
    fwrite(&record, 1, sizeof(record), f);
    fclose(f);
    
    // 4. Update index
    if (sensor_idx .record_count == 0) {
        sensor_idx .first_timestamp = record.timestamp;
    }
    sensor_idx .last_timestamp = record.timestamp;
    sensor_idx .record_count++;
    if (sensor_idx .record_count > sensor_idx .max_records) {
        sensor_idx .record_count = sensor_idx .max_records;
    }
    
    // 5. Advance write pointer (wrap at end)
    sensor_idx .write_offset += sizeof(record);
    if (sensor_idx .write_offset >= HISTORY_FILE_SIZE) {
        sensor_idx .write_offset = 0;
        // When wrapping, update first_timestamp to the next record
        // Read the record at the new offset to get its timestamp
        // This is done on next read
    }
    
    // 6. Save index
    history_write_sensor_idx();
}

int sensor_history_get_range(uint32_t start_ts, uint32_t end_ts,
                             sensor_record_t *buffer, int max_records) {
    if (!history_initialized) return 0;
    if (sensor_idx .record_count == 0) return 0;
    
    // Open file
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (!f) return 0;
    
    int found = 0;
    uint32_t offset = 0;
    
    // Scan through the file
    for (uint32_t i = 0; i < sensor_idx .record_count && found < max_records; i++) {
        sensor_record_t record;
        fseek(f, offset, SEEK_SET);
        if (fread(&record, 1, sizeof(record), f) != sizeof(record)) {
            break;
        }
        
        if (record.timestamp >= start_ts && record.timestamp <= end_ts) {
            memcpy(&buffer[found], &record, sizeof(record));
            found++;
        }
        
        offset += sizeof(record);
        if (offset >= HISTORY_FILE_SIZE) {
            offset = 0;
        }
    }
    
    fclose(f);
    return found;
}

int sensor_history_get_latest(sensor_record_t *record) {
    if (!history_initialized || sensor_idx .record_count == 0) return -1;
    
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (!f) return -1;
    
    // Read the last written record (at write_offset - sizeof(record))
    uint32_t offset = sensor_idx .write_offset - sizeof(record);
    if (offset >= HISTORY_FILE_SIZE) {
        offset = 0;
    }
    
    fseek(f, offset, SEEK_SET);
    size_t read = fread(record, 1, sizeof(sensor_record_t), f);
    fclose(f);
    
    return (read == sizeof(sensor_record_t)) ? 0 : -1;
}

uint32_t sensor_history_get_oldest_ts(void) {
    return sensor_idx .first_timestamp;
}

uint32_t sensor_history_get_newest_ts(void) {
    return sensor_idx .last_timestamp;
}

uint32_t sensor_history_get_record_count(void) {
    return sensor_idx .record_count;
}

int sensor_history_export_csv(uint32_t start_ts, uint32_t end_ts,
                              void (*write_callback)(const char *)) {
    if (!history_initialized) return -1;
    
    // Get records
    sensor_record_t records[100];
    int count = sensor_history_get_range(start_ts, end_ts, records, 100);
    if (count == 0) return 0;
    
    // Write header
    write_callback("Timestamp,pH,EC,Potassium,Magnesium,Iron,Phosphorus,Calcium,Nitrogen,Temperature,Humidity\n");
    
    // Write each record
    char line[256];
    for (int i = 0; i < count; i++) {
        sensor_record_t *r = &records[i];
        
        // Convert timestamp to string
        char ts_str[32];
        time_t ts = r->timestamp;
        struct tm *tm_info = localtime(&ts);
        strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        snprintf(line, sizeof(line),
                 "%s,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%d\n",
                 ts_str,
                 r->values[0],  // pH
                 r->values[1],  // EC
                 r->values[2],  // K
                 r->values[3],  // Mg
                 r->values[4],  // Fe
                 r->values[5],  // P
                 r->values[6],  // Ca
                 r->values[7],  // N
                 r->values[8] / 10.0f,  // Temp (℃ × 10)
                 r->values[9]); // Humidity
        
        write_callback(line);
    }
    
    return count;
}
