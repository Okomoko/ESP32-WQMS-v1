// sensor_history.c
// 32-day ring buffer implementation

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_task_wdt.h"

#include "sensor_history.h"
#include "sensor_read.h"
#include "log_levels.h"
#include "system_config.h"
#include "nvs_config.h"
#include "ntp_client.h"
#include "esp_littlefs.h"

// ============================================================
// Data Structures
// ============================================================
typedef struct __attribute__((packed)) {
    uint32_t magic;           // 0x53454E53 ("SENS")
    uint32_t version;         // 1
    uint32_t max_records;     // (15 days)
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

static void set_default_index(void) {
    sensor_idx.magic = 0x53454E53;
    sensor_idx.version = 1;
    sensor_idx.max_records = HISTORY_MAX_RECORDS;
    sensor_idx.write_offset = 0;
    sensor_idx.record_count = 0;
    sensor_idx.first_timestamp = 0;
    sensor_idx.last_timestamp = 0;
    sensor_idx.sensor_count = TOTAL_SENSOR_COUNT;
    sensor_idx.sample_interval = 60;
    memset(sensor_idx.reserved, 0, sizeof(sensor_idx.reserved));
}

static int history_write_sensor_idx(void) {
    // Try to open in read/write mode (no truncation)
    FILE *f = fopen(INDEX_FILE, "r+b");
    if (!f) {
        // File doesn't exist - create and pre-allocate it
        f = fopen(INDEX_FILE, "wb");
        if (!f) {
            SENSOR_LOG_E("Failed to create index file");
            return -1;
        }
        // Pre-allocate one block (256 bytes) with zeros
        uint8_t zero[256] = {0};
        size_t written = fwrite(zero, 1, sizeof(zero), f);
        fclose(f);
        if (written != sizeof(zero)) {
            SENSOR_LOG_E("Failed to pre-allocate index file");
            return -1;
        }
        // Reopen in "r+b" mode
        f = fopen(INDEX_FILE, "r+b");
        if (!f) {
            SENSOR_LOG_E("Failed to reopen index file");
            return -1;
        }
    }
    
    fseek(f, 0, SEEK_SET);
    size_t written = fwrite(&sensor_idx, 1, sizeof(sensor_idx), f);
    fclose(f);
    
    if (written != sizeof(sensor_idx)) {
        SENSOR_LOG_E("Failed to write index file");
        return -1;
    }
    
    SENSOR_LOG_D("History index written successfully.");
    return 0;
}

static int history_read_index(void) {
    FILE *f = fopen(INDEX_FILE, "rb");
    if (!f) {
        SENSOR_LOG_D("History index does not exist, creating defaults.");
        set_default_index();
        return -1;  // Signal that file doesn't exist
    }
    
    size_t read = fread(&sensor_idx, 1, sizeof(sensor_idx), f);
    fclose(f);
    
    if (read != sizeof(sensor_idx) || sensor_idx.magic != 0x53454E53) {
        SENSOR_LOG_E("Invalid index file, recreating defaults");
        set_default_index();
        return -1;  // Signal that index was invalid
    }

    SENSOR_LOG_D("History index loaded: %lu records, newest=%lu", sensor_idx.record_count, sensor_idx.last_timestamp);
    return 0;  // Success
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
    // Just create an empty file - no pre-allocation
    // The file will grow naturally as records are added
    FILE *f = fopen(HISTORY_FILE, "wb");
    if (!f) {
        SENSOR_LOG_E("Failed to create history file");
        return -1;
    }
    fclose(f);
    SENSOR_LOG_D("History file created (empty)");
    return 0;
}

// ============================================================
// Public Functions - FULLY SYNCHRONOUS
// ============================================================

void sensor_history_init(void) {
    int ret;
    
    SENSOR_LOG_D("=== SENSOR HISTORY INITIALIZATION START ===");
    
    // ============================================================
    // STEP 1: Try to read existing index
    // ============================================================
//    SENSOR_LOG_D("Step 1: Reading index...");
    ret = history_read_index();
    
    // ============================================================
    // STEP 2: If index doesn't exist or is invalid, create it
    // ============================================================
    if (ret != 0) {
//        SENSOR_LOG_D("Step 2: Creating new index with defaults...");
        
        // Ensure index file is pre-allocated
        FILE *idx = fopen(INDEX_FILE, "rb");
        if (!idx) {
            // Create and pre-allocate index file
            idx = fopen(INDEX_FILE, "wb");
            if (!idx) {
                SENSOR_LOG_E("FATAL: Failed to create index file");
                return;
            }
            uint8_t zero[256] = {0};
            if (fwrite(zero, 1, sizeof(zero), idx) != sizeof(zero)) {
                SENSOR_LOG_E("FATAL: Failed to pre-allocate index file");
                fclose(idx);
                return;
            }
            fclose(idx);
//            SENSOR_LOG_D("Index file pre-allocated");
        } else {
            fclose(idx);
        }
        
        // Write the default index to disk
        if (history_write_sensor_idx() != 0) {
            SENSOR_LOG_E("FATAL: Failed to write index");
            return;
        }
//        SENSOR_LOG_D("Default index written to disk");
    }
    
    // ============================================================
    // STEP 3: Create history file if it doesn't exist
    // ============================================================
//    SENSOR_LOG_D("Step 3: Checking history file...");
    if (!history_file_exists()) {
//        SENSOR_LOG_D("History file missing - creating...");
        
        // Reset index to defaults (in case we had stale data)
        set_default_index();
        
        // Write the reset index to disk BEFORE creating history file
        if (history_write_sensor_idx() != 0) {
            SENSOR_LOG_E("FATAL: Failed to write reset index");
            return;
        }
//        SENSOR_LOG_D("Reset index written to disk");
        
        // Now create the history file (empty, will grow naturally)
        if (history_create_file() != 0) {
            SENSOR_LOG_E("FATAL: Failed to create history file");
            return;
        }
//        SENSOR_LOG_D("History file created");
    } else {
//        SENSOR_LOG_D("History file exists");
    }
    
    // ============================================================
    // STEP 4: Verify index is readable and valid
    // ============================================================
//    SENSOR_LOG_D("Step 4: Verifying index...");
    
    // Read the index again to verify it was written correctly
    ret = history_read_index();
    if (ret != 0) {
        SENSOR_LOG_E("FATAL: Index verification failed - cannot read valid index");
        return;
    }
//    SENSOR_LOG_D("Index verification passed");
    
    // ============================================================
    // STEP 5: Verify history file is accessible
    // ============================================================
//    SENSOR_LOG_D("Step 5: Verifying history file...");
    if (!history_file_exists()) {
        SENSOR_LOG_E("FATAL: History file missing after creation");
        return;
    }
    
    // Test write/read to history file
    FILE *test = fopen(HISTORY_FILE, "r+b");
    if (!test) {
        SENSOR_LOG_E("FATAL: Cannot open history file for read/write");
        return;
    }
    fclose(test);
//    SENSOR_LOG_D("History file verified");
    
    // ============================================================
    // STEP 6: Get partition info for debugging
    // ============================================================
//    SENSOR_LOG_D("Step 6: Getting partition info...");
    size_t total, used;
    if (esp_littlefs_info("sensors", &total, &used) == ESP_OK) {
//        SENSOR_LOG_D("Partition: total=%u bytes, used=%u bytes, free=%u bytes, blocks=%u", total, used, total - used, total / 4096);
    }
    
    // ============================================================
    // STEP 7: All done - set initialized flag
    // ============================================================
    history_initialized = 1;
    
    SENSOR_LOG_D("=== SENSOR HISTORY INITIALIZATION COMPLETE ===");
//    SENSOR_LOG_D("Records: %lu of %lu max", sensor_idx.record_count, sensor_idx.max_records);
//    SENSOR_LOG_D("First timestamp: %lu", sensor_idx.first_timestamp);
//    SENSOR_LOG_D("Last timestamp: %lu", sensor_idx.last_timestamp);
//    SENSOR_LOG_D("Write offset: %lu bytes", sensor_idx.write_offset);
//    SENSOR_LOG_D("=============================================");
}

void sensor_history_add(void) {
    // Safety check - ensure initialization completed
    if (!history_initialized) {
        SENSOR_LOG_E("ERROR: sensor_history_add called but history not initialized!");
        return;
    }

    SENSOR_LOG_D("Reading the sensors...");
            
    // 1. Get current readings
    sensor_reading_t *readings = sensor_get_all_readings();
    if (!readings){
        SENSOR_LOG_E("sensor_get_all_readings failed.");
        return;
    }

    // 2. Build record
    sensor_record_t record = {0};
    record.timestamp = time(NULL);
    record.sensor_mask = 0;
    
    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
        if (readings[i].status == SENSOR_STATUS_OK) {
            record.sensor_mask |= (1 << i);
            record.values[i] = (float)readings[i].value;
//            SENSOR_LOG_D("%d, 1 - %d", i, record.sensor_mask);
//            SENSOR_LOG_D("%d -> %f", i, record.values[i]);
        } else {
            record.sensor_mask &= ~(1 << i);
//            SENSOR_LOG_D("%d, 0 - %d", i, record.sensor_mask);
            record.values[i] = 0;
        }
    }

    // 3. Write to file at current offset
    FILE *f = fopen(HISTORY_FILE, "r+b");
    if (!f) {
        SENSOR_LOG_E("Failed to open history file for writing");
        return;
    }

    // Get current file size to handle natural growth
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Determine write position
    uint32_t write_offset = sensor_idx.write_offset;
    
    // If file is not yet full and write_offset is beyond current end,
    // append at the end (natural growth)
    if (file_size < HISTORY_FILE_SIZE) {
        if (write_offset >= (uint32_t)file_size) {
            write_offset = file_size;
        }
    }
    // else file is full - use ring offset as is

    int8_t ret;
    if ((ret = fseek(f, write_offset, SEEK_SET)) != 0) {
        SENSOR_LOG_E("Failed to seek to the writing position!(%d)", ret);
        fclose(f);
        return;
    }
    
    if ((ret = fwrite(&record, 1, sizeof(record), f)) != sizeof(record)) {
        SENSOR_LOG_E("Failed to write the record!(%d)", ret);
        fclose(f);
        return;
    }
    
    int close_ret = fclose(f);
    if (close_ret != 0) {
        SENSOR_LOG_E("fclose failed, errno=%d", close_ret);
        return;  // Don't update index if commit failed
    }
    
    // 4. Update index
    if (sensor_idx.record_count == 0) {
        sensor_idx.first_timestamp = record.timestamp;
    }
    sensor_idx.last_timestamp = record.timestamp;
    sensor_idx.record_count++;
    if (sensor_idx.record_count > sensor_idx.max_records) {
        sensor_idx.record_count = sensor_idx.max_records;
    }
    
    // 5. Advance write pointer (wrap at end)
    sensor_idx.write_offset += sizeof(record);
    if (sensor_idx.write_offset >= HISTORY_FILE_SIZE) {
        sensor_idx.write_offset = 0;
        // When wrapping, update first_timestamp to the next record
        // Read the record at the new offset to get its timestamp
        // This is done on next read
    }
    
    // 6. Save index
    if (history_write_sensor_idx() != 0) {
        SENSOR_LOG_E("Failed to save index");
        return;
    }
    
    SENSOR_LOG_D("New sensor data is appended to history file successfully.");
}

int sensor_history_get_records(uint32_t starting_offset, sensor_record_t *buffer, uint32_t number_of_records) {
    if (!history_initialized) {
        SENSOR_LOG_E("ERROR: sensor_history_get_records called but history not initialized!");
        return -1;
    }
    if (sensor_idx.record_count == 0) {
        SENSOR_LOG_D("Index says there is no record.");
        return -1;
    }
    
    // Open file
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (!f) {
        SENSOR_LOG_E("Failed to open history file for reading");
        return -1;
    }
    
    int found = 0;
    uint32_t offset = (starting_offset - 1) * sizeof(sensor_record_t);
    
    // Scan through the file
//    SENSOR_LOG_D("Sensor history record offset - number of records: %u - %u", starting_offset, number_of_records);
    for (uint32_t i = 0; i < sensor_idx.record_count && found < number_of_records; i++) {
        sensor_record_t record;
        fseek(f, offset, SEEK_SET);
        if (fread(&record, 1, sizeof(record), f) != sizeof(record)) {
            SENSOR_LOG_E("Sensor history record cannot be read!");
            break;
        }

        memcpy(&buffer[found], &record, sizeof(record));
        found++;

        offset += sizeof(record);
        if (offset >= HISTORY_FILE_SIZE) {
            SENSOR_LOG_E("End of Sensor history file reached!");
            break;
        }
    }
    
    fclose(f);
    SENSOR_LOG_V("%d records found.", found);
    return found;
}
    
int sensor_history_get_range(uint32_t start_ts, uint32_t end_ts, sensor_record_t *buffer, int max_records) {
    if (!history_initialized) {
        SENSOR_LOG_E("ERROR: sensor_history_get_range called but history not initialized!");
        return -1;
    }
    if (sensor_idx.record_count == 0) {
        SENSOR_LOG_D("Index says there is no record.");
        return -1;
    }
    
    // Open file
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (!f) {
        SENSOR_LOG_E("Failed to open history file for reading");
        return -1;
    }
    
    int found = 0;
    uint32_t offset = 0;
    
    // Scan through the file
//    SENSOR_LOG_D("Sensor history record timestamp range: %d - %d", start_ts, end_ts);
    for (uint32_t i = 0; i < sensor_idx.record_count && found < max_records; i++) {
        sensor_record_t record;
        fseek(f, offset, SEEK_SET);
        if (fread(&record, 1, sizeof(record), f) != sizeof(record)) {
            SENSOR_LOG_E("Sensor history record cannot be read!");
            break;
        }
        SENSOR_LOG_V("Sensor history record timestamp:%d", record.timestamp);
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
    SENSOR_LOG_D("%d records found.", found);
    return found;
}

int sensor_history_get_latest(sensor_record_t *record) {
    if (!history_initialized || sensor_idx.record_count == 0) return -1;
    
    FILE *f = fopen(HISTORY_FILE, "rb");
    if (!f) {
        SENSOR_LOG_E("Failed to open history file for reading");
        return -1;
    }
    
    // Read the last written record (at write_offset - sizeof(record))
    uint32_t offset = (sensor_idx.record_count - 1) * sizeof(sensor_record_t);
    if (offset >= HISTORY_FILE_SIZE) {
        offset = 0;
    }
    
    fseek(f, offset, SEEK_SET);
    size_t read = fread(record, 1, sizeof(sensor_record_t), f);
    fclose(f);
    SENSOR_LOG_D("Sensor History read size...%d", read);
    
    return (read == sizeof(sensor_record_t)) ? 0 : -1;
}

uint32_t sensor_history_get_oldest_ts(void) {
    return sensor_idx.first_timestamp;
}

uint32_t sensor_history_get_newest_ts(void) {
    return sensor_idx.last_timestamp;
}

uint32_t sensor_history_get_record_count(void) {
    return sensor_idx.record_count;
}