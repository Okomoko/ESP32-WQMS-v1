// http_server.c
// HTTP server with partition-based web assets and boot-time sync from Supabase

#include <string.h>
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "http_server.h"
#include "api_handler.h"
#include "api_config.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "watchdog.h"
#include "web_console.h"
#include "nvs_config.h"
#include "cert_manager.h"

// ============================================================
// Static Variables
// ============================================================
static httpd_handle_t server = NULL;

// ============================================================
// File Info Structure
// ============================================================
typedef struct {
    char name[256];
    uint64_t size;
    uint32_t last_modified;
} file_info_t;

static char supabase_cert_buffer[SSL_CERTIFICATE_MAX_SIZE];

// ============================================================
// Helper: Parse Supabase manifest file
// ============================================================
static int parse_supabase_manifest(const char *json_str, file_info_t **files) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return 0;
    
    // Get the objects array
    cJSON *objects = cJSON_GetObjectItem(root, "objects");
    if (!cJSON_IsArray(objects)) {
        cJSON_Delete(root);
        return 0;
    }
    
    int count = cJSON_GetArraySize(objects);

    // First pass: count how many files are in web_assets folder
    int valid_count = 0;
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(objects, i);
        if (!item || !cJSON_IsString(item)) continue;
        
        const char *path = item->valuestring;
        
        // Check if this file is in the web_assets folder
        if (strncmp(IoT_UPDATE_PATH, path, strlen(IoT_UPDATE_PATH)) == 0) {
            valid_count++;
        }
    }
    
    if (valid_count == 0) {
        cJSON_Delete(root);
        return 0;
    }
    
    // Allocate memory only for web_assets files
    *files = malloc(valid_count * sizeof(file_info_t));
    if (!*files) {
        cJSON_Delete(root);
        return 0;
    }
    
    // Get the generated_at timestamp
    uint32_t manifest_timestamp = 0;
    cJSON *generated_at = cJSON_GetObjectItem(root, "generated_at");
    if (generated_at && cJSON_IsString(generated_at)) {
        // Parse ISO 8601 timestamp
        struct tm tm = {0};
        if (sscanf(generated_at->valuestring, "%d-%d-%dT%d:%d:%d", 
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            manifest_timestamp = timegm(&tm);
        }
    }
    
    // Second pass: extract web_assets files
    int idx = 0;
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(objects, i);
        if (!item || !cJSON_IsString(item)) continue;
        
        const char *path = item->valuestring;
        
        // Only process files in the web_assets folder
        if (strncmp(IoT_UPDATE_PATH, path, strlen(IoT_UPDATE_PATH)) != 0) {
            continue; // Skip files in other folders
        }
        
        // Extract just the filename from the path (e.g., "web_assets/index.html" -> "index.html")
        const char *name = &path[strlen(IoT_UPDATE_PATH)];
        
        // Copy the name
        strncpy((*files)[idx].name, name, 255);
        (*files)[idx].name[255] = '\0';
        
        // Use the manifest timestamp
        (*files)[idx].last_modified = manifest_timestamp;
        (*files)[idx].size = 0; // Size not available from manifest
        
        idx++;
    }
    
    cJSON_Delete(root);
    return valid_count;
}

// ============================================================
// Helper: Get list of files from Supabase manifest
// ============================================================
static int get_supabase_file_list(file_info_t **files) {
    char url[270];
    const char *api_key = nvs_get_supabase_api_key();
    
    // Use manifest URL instead of bucket list URL
    // Format: https://project.supabase.co/storage/v1/object/public/BUCKET/manifest.json
    char bucketdownloadurl[256];
    nvs_get_supabase_bucketdownload_url(bucketdownloadurl, sizeof(bucketdownloadurl));
    snprintf(url, sizeof(url), "%s/manifest.json", bucketdownloadurl);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 2,
        .keep_alive_count = 3,
        .skip_cert_common_name_check = false,
    };
    
    // Load certificate from NVS using generic manager
    size_t cert_len = 0;
    
    if (cert_manager_has(CERT_TYPE_SUPABASE)) {
        esp_err_t err = cert_manager_load(CERT_TYPE_SUPABASE, supabase_cert_buffer, SSL_CERTIFICATE_MAX_SIZE, &cert_len);
        if (err == ESP_OK && cert_len > 0) {
            supabase_cert_buffer[cert_len] = 0;
            supabase_cert_buffer[cert_len + 1] = 0;
            config.cert_pem = supabase_cert_buffer;
            config.cert_len = cert_len + 1;
            
//            WQMS_LOG_D("Using Supabase certificate from NVS (%zu bytes)", cert_len);
        } else {
            WQMS_LOG_W("Failed to load Supabase certificate: %s", esp_err_to_name(err));
            return 0;
        }
    } else {
        WQMS_LOG_E("No Supabase certificate in NVS, aborting...");
        return 0;
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return 0;
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", api_key);
    esp_http_client_set_header(client, "Authorization", api_key);
    
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return 0;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return 0;
    }
    
    char *response = malloc(content_length + 1);
    if (!response) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return 0;
    }
    
    int total_read = 0;
    int read_len;
    while ((read_len = esp_http_client_read(client, response + total_read, content_length - total_read)) > 0) {
        total_read += read_len;
    }
    response[total_read] = '\0';
    
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    int count = parse_supabase_manifest(response, files);
    free(response);
    
    return count;
}

// ============================================================
// Helper: Download a file from Supabase
// ============================================================
static bool download_file_from_supabase(const char *filename, const char *dest_path) {
    char url[512];
    char bucketdownloadurl[256];
    const char *api_key = nvs_get_supabase_api_key();
    nvs_get_supabase_bucketdownload_url(bucketdownloadurl, sizeof(bucketdownloadurl));

    snprintf(url, sizeof(url), "%s/%s", bucketdownloadurl, filename);
    WQMS_LOG_D("Download URL : %s", url);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 2,
        .keep_alive_count = 3,
        .skip_cert_common_name_check = false,
        .buffer_size = 4096,
    };
    
    // Load certificate from NVS using generic manager
    size_t cert_len = 0;
    if (cert_manager_has(CERT_TYPE_SUPABASE)) {
        esp_err_t err = cert_manager_load(CERT_TYPE_SUPABASE, supabase_cert_buffer, SSL_CERTIFICATE_MAX_SIZE, &cert_len);
        if (err == ESP_OK && cert_len > 0) {
            supabase_cert_buffer[cert_len] = 0;
            supabase_cert_buffer[cert_len + 1] = 0;
            config.cert_pem = supabase_cert_buffer;
            config.cert_len = cert_len + 1;
            WQMS_LOG_D("Using Supabase certificate from NVS (%zu bytes)", cert_len);
        } else {
            WQMS_LOG_W("Failed to load Supabase certificate: %s", esp_err_to_name(err));
            return false;
        }
    } else {
        WQMS_LOG_D("No Supabase certificate in NVS, aborting...");
        return false;
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "apikey", api_key);
    esp_http_client_set_header(client, "Authorization", api_key);

    // Step 1: Open the connection
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        WQMS_LOG_E("Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Step 2: Fetch headers and get content length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        WQMS_LOG_E("Failed to fetch headers: %d", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    
    int status_code = esp_http_client_get_status_code(client);
  
    if (status_code != 200) {
        WQMS_LOG_E("HTTP error: %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (content_length <= 0) {
        WQMS_LOG_W("Content length is 0 or negative");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Step 3: Check if there's enough heap memory
    size_t free_heap = esp_get_free_heap_size();
    WQMS_LOG_D("Free heap: %zu bytes, required: %d bytes", free_heap, content_length);
    
    // Add 4096 bytes safety margin for other operations
    if (free_heap < (size_t)(content_length + 4096)) {
        WQMS_LOG_E("Insufficient heap memory: free %zu, required %d + 4KB safety", 
                   free_heap, content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Step 4: Allocate memory for the downloaded file
    unsigned char *file_buffer = malloc(content_length);
    if (!file_buffer) {
        WQMS_LOG_E("Failed to allocate %d bytes for download", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Step 5: Read the entire file directly into the buffer
    int total_read = esp_http_client_read(client, (char *)file_buffer, content_length);
    
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // Step 6: Verify the download
    if (total_read < content_length) {
        WQMS_LOG_E("Download incomplete: %d of %d bytes", total_read, content_length);
        free(file_buffer);
        return false;
    }

    WQMS_LOG_I("Download complete: %s (%d bytes)", filename, total_read);

    // Step 7: Write the buffer to file in chunks
    WQMS_LOG_D("Attempting to write to: %s", dest_path);
    
    // Check if the file already exists and log its size
    struct stat st;
    if (stat(dest_path, &st) == 0) {
        WQMS_LOG_D("Existing file size: %lld bytes", (long long)st.st_size);
    }
    
    // Try to create the directory path if it doesn't exist
    char dir_path[260];
    strncpy(dir_path, dest_path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        // Try to create directory (ignore if it exists)
        mkdir(dir_path, 0777);
        WQMS_LOG_D("Created directory (or already exists): %s", dir_path);
    }
    
    FILE *f = fopen(dest_path, "wb");
    if (!f) {
        WQMS_LOG_E("Failed to create file: %s", dest_path);
        WQMS_LOG_E("errno: %d (%s)", errno, strerror(errno));
        
        // Check if we can write to the directory
        DIR *dir = opendir("/www");
        if (dir) {
            WQMS_LOG_D("Directory /www is accessible");
            closedir(dir);
        } else {
            WQMS_LOG_E("Directory /www is NOT accessible");
        }
        
        free(file_buffer);
        return false;
    }

    WQMS_LOG_D("File opened successfully, writing %d bytes in chunks", total_read);
    
    // Step 8: Write in 10KB chunks with delays
    const size_t CHUNK_SIZE = 10240; // 10KB chunks
    size_t bytes_written_total = 0;
    bool write_success = true;
    int chunk_count = 0;
    
    while (bytes_written_total < (size_t)total_read) {
        size_t remaining = total_read - bytes_written_total;
        size_t chunk_size = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        
        size_t written = fwrite(file_buffer + bytes_written_total, 1, chunk_size, f);
        if (written != chunk_size) {
            WQMS_LOG_E("Failed to write chunk %d: wrote %zu of %zu bytes", 
                       chunk_count + 1, written, chunk_size);
            write_success = false;
            break;
        }
        
        bytes_written_total += written;
        chunk_count++;
        
        // Log progress every 10 chunks
/*        if (chunk_count % 10 == 0) {
            WQMS_LOG_D("Written %zu/%d bytes (%d chunks)", 
                       bytes_written_total, total_read, chunk_count);
        }
*/
        // Wait 10-15ms between chunks (except for the last chunk)
        if (bytes_written_total < (size_t)total_read) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    WQMS_LOG_D("Total bytes written: %zu in %d chunks", bytes_written_total, chunk_count);
    
    int ferror_code = ferror(f);
    if (ferror_code) {
        WQMS_LOG_E("File error after write: %d", ferror_code);
        write_success = false;
    }
    
    int fclose_result = fclose(f);
    WQMS_LOG_D("fclose result: %d", fclose_result);
    
    free(file_buffer);

    if (!write_success || bytes_written_total != (size_t)total_read) {
        WQMS_LOG_E("Failed to write file: wrote %zu of %d bytes", bytes_written_total, total_read);
        unlink(dest_path);
        return false;
    }

    // Verify the file was written correctly
    FILE *verify_f = fopen(dest_path, "rb");
    if (verify_f) {
        fseek(verify_f, 0, SEEK_END);
        long file_size = ftell(verify_f);
        fclose(verify_f);
        WQMS_LOG_D("Verified file size: %ld bytes", file_size);
        
        if (file_size != total_read) {
            WQMS_LOG_E("File size mismatch: expected %d, got %ld", total_read, file_size);
            unlink(dest_path);
            return false;
        }
    } else {
        WQMS_LOG_E("Failed to verify written file");
        return false;
    }

    WQMS_LOG_I("Successfully saved: %s (%d bytes) in %d chunks", filename, total_read, chunk_count);
    return true;
}

// ============================================================
// Helper: Get file modification time from ESP32
// ============================================================
static uint32_t get_local_file_time(const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

// ============================================================
// Helper: Check if file exists in ESP32
// ============================================================
static bool file_exists_local(const char *filepath) {
    struct stat st;
    return (stat(filepath, &st) == 0);
}

// ============================================================
// Helper: Get last update timestamp from ESP32
// ============================================================
static uint32_t get_last_update_time(void) {
    char timestamp_file[64];
    snprintf(timestamp_file, sizeof(timestamp_file), "%s/.last_update", WEB_BASE_PATH);
    
    FILE *f = fopen(timestamp_file, "r");
    if (!f) return 0;
    
    uint32_t timestamp;
    if (fscanf(f, "%lu", &timestamp) != 1) {
        timestamp = 0;
    }
    fclose(f);
    return timestamp;
}

// ============================================================
// Helper: Save last update timestamp to ESP32
// ============================================================
static void save_last_update_time(uint32_t timestamp) {
    char timestamp_file[64];
    snprintf(timestamp_file, sizeof(timestamp_file), "%s/.last_update", WEB_BASE_PATH);
    
    FILE *f = fopen(timestamp_file, "w");
    if (f) {
        fprintf(f, "%lu\n", timestamp);
        fclose(f);
    }
}

// ============================================================
// Helper: Find latest file timestamp in Supabase list
// ============================================================
static uint32_t get_latest_supabase_timestamp(file_info_t *files, int count) {
    uint32_t latest = 0;
    for (int i = 0; i < count; i++) {
        if (files[i].last_modified > latest) {
            latest = files[i].last_modified;
        }
    }
    return latest;
}

// ============================================================
// Helper: Sync web assets from Supabase (incremental)
// ============================================================
static void sync_web_assets_from_supabase(void) {
    WQMS_LOG_I("Checking Supabase for web asset updates...");
    
    // Step 1: Get last update timestamp from ESP32
    uint32_t local_timestamp = get_last_update_time();
    WQMS_LOG_I("Local last update: %lu", local_timestamp);
    
    // Step 2: Get file list from Supabase
    file_info_t *supabase_files = NULL;
    int supabase_count = get_supabase_file_list(&supabase_files);
    
    if (supabase_count <= 0) {
        WQMS_LOG_W("No files found in Supabase bucket or failed to fetch list");
        if (supabase_files) free(supabase_files);
        return;
    }
    
    WQMS_LOG_I("Found %d files in Supabase bucket", supabase_count);
    
    // Step 3: Get latest timestamp from Supabase
    uint32_t supabase_timestamp = get_latest_supabase_timestamp(supabase_files, supabase_count);
    WQMS_LOG_I("Supabase latest timestamp: %lu", supabase_timestamp);
    
    // Step 4: Compare timestamps - skip if already up to date
    if (local_timestamp >= supabase_timestamp) {
        WQMS_LOG_I("Assets are up to date (local: %lu >= remote: %lu)", 
                  local_timestamp, supabase_timestamp);
        free(supabase_files);
        return;
    }

    // Step 5: Remove orphaned files (exist locally but not in Supabase)
    int removed = 0;
    DIR *dir = opendir(WEB_BASE_PATH);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            WQMS_LOG_D("File : %s", entry->d_name);
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            // Skip .last_update file
            if (strcmp(entry->d_name, ".last_update") == 0) {
                continue;
            }
            bool found = false;
            for (int i = 0; i < supabase_count; i++) {
                if (strcmp(entry->d_name, supabase_files[i].name) == 0) {
                    found = true;
                    break;
                }
            }

            // If not found in Supabase, delete it
            if (!found) {
                char file_path[256];
                snprintf(file_path, sizeof(file_path), "%s/%.240s", WEB_BASE_PATH, entry->d_name);
                
                struct stat st;
                if (stat(file_path, &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        // Remove directory (simplified - could be recursive)
                        if (rmdir(file_path) == 0) {
                            WQMS_LOG_I("Removed orphan directory: %s", entry->d_name);
                            removed++;
                        }
                    } else {
                        if (unlink(file_path) == 0) {
                            WQMS_LOG_I("Removed orphan file: %s", entry->d_name);
                            removed++;
                        }
                    }
                }
            }
        }
        closedir(dir);
    }

    WQMS_LOG_I("Newer assets found on Supabase, updating...");

    // Step 6: Update or add files from Supabase
    int updated = 0;
    int added = 0;
    int failed = 0;
    
    for (int i = 0; i < supabase_count; i++) {
        char dest_path[256];
        char rmt_path[256];
        snprintf(dest_path, sizeof(dest_path), "%s/%.240s", WEB_BASE_PATH, supabase_files[i].name);
        snprintf(rmt_path, sizeof(rmt_path), "%s%.240s", IoT_UPDATE_PATH, supabase_files[i].name);
        
        WQMS_LOG_D("%s: ", dest_path);

        uint32_t local_time = get_local_file_time(dest_path);
        bool exists = file_exists_local(dest_path);
        WQMS_LOG_I("%s: %s -> %s (remote: %lu, local: %lu)", 
                  exists ? "Updating" : "Adding",
                  rmt_path,
                  dest_path,
                  supabase_files[i].last_modified, 
                  local_time);

        // Check if file needs update (newer on Supabase or doesn't exist locally)
        if (!exists || supabase_files[i].last_modified > local_time) {
            if (download_file_from_supabase(rmt_path, dest_path)) {
                // rely on .last_update file for the last updated timestamp
                if (exists) {
                    updated++;
                } else {
                    added++;
                }
            } else {
                WQMS_LOG_E("Failed to download: %s", supabase_files[i].name);
                failed++;
            }
        } else {
            WQMS_LOG_D("File up to date: %s", supabase_files[i].name);
        }
    }

    // Step 7: Update local timestamp if any changes were made
    if (updated > 0 || added > 0 || removed > 0) {
        WQMS_LOG_I("Sync complete: %d added, %d updated, %d removed, %d failed.", 
                  added, updated, removed, failed);
        if (failed == 0) {
            WQMS_LOG_I("Sync timestamp updated to %lu", supabase_timestamp);
        } else {
            WQMS_LOG_W("Sync had failures, timestamp not updated");
        }
    } else if (failed == 0) {
        // No changes but timestamp mismatch - update timestamp anyway
        save_last_update_time(supabase_timestamp);
        WQMS_LOG_I("No changes needed, timestamp updated to %lu", supabase_timestamp);
    } else {
        WQMS_LOG_W("Sync had failures, timestamp not updated");
    }
    
    free(supabase_files);
}

// ============================================================
// Helper: Serve File from Partition with Wildcard Support
// ============================================================
static esp_err_t serve_file_from_partition(httpd_req_t *req) {
    // Get the requested URI path
    const char *uri = req->uri;
    
    // Skip leading slash and construct full path
    char file_path[256];
    if (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/index.html", WEB_BASE_PATH);
    } else {
        snprintf(file_path, sizeof(file_path), "%s/%.240s", WEB_BASE_PATH, uri);
    }
//    WQMS_LOG_D("File to be served : %s", file_path);
    // Check if file exists in file system
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Determine MIME type based on file extension
    const char *ext = strrchr(file_path, '.');
    const char *mime_type = "application/octet-stream";
    
    if (ext) {
        if (strcmp(ext, ".html") == 0) mime_type = "text/html";
        else if (strcmp(ext, ".css") == 0) mime_type = "text/css";
        else if (strcmp(ext, ".js") == 0) mime_type = "application/javascript";
        else if (strcmp(ext, ".ico") == 0) mime_type = "image/x-icon";
        else if (strcmp(ext, ".png") == 0) mime_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) mime_type = "image/jpeg";
        else if (strcmp(ext, ".svg") == 0) mime_type = "image/svg+xml";
        else if (strcmp(ext, ".json") == 0) mime_type = "application/json";
    }

    // Open and send the file
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, mime_type);
    
    // Read and send file in chunks
    char chunk[512];
    size_t bytes_read;
    while ((bytes_read = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, bytes_read) != ESP_OK) {
            fclose(file);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ============================================================
// Public Functions
// ============================================================

void webserver_init(void) {
    // Step 1: Sync assets from Supabase during boot
    if (littlefs_web_assets_mounted) {
        // Note: WiFi should be connected before calling this function
        sync_web_assets_from_supabase();
    } else {
        WQMS_LOG_E("HTTP server partition is not mounted");
    }

    // Step 2: Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 64;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = STACK_SIZE_WEBSERVER;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        WQMS_LOG_I("HTTP server started on port %d", config.server_port);

        // ============================================================
        // Register API Endpoints (these will match before wildcard)
        // ============================================================
        register_api_endpoints(server);
        
        // ============================================================
        // Register Web Console Handler
        // ============================================================
        httpd_uri_t web_console_handler_uri = {
            .uri       = "/console",
            .method    = HTTP_GET,
            .handler   = web_console_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &web_console_handler_uri);
        
        // ============================================================
        // SINGLE WILDCARD HANDLER for all web files
        // ============================================================
        httpd_uri_t file_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = serve_file_from_partition,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &file_uri);
        
        // ============================================================
        // Register with Watchdog
        // ============================================================
        watchdog_register_module(WDT_MODULE_WEBSERVER, 10);

        WQMS_LOG_I("Web server initialized with partition-based assets");
    } else {
        WQMS_LOG_E("Failed to start HTTP server");
    }
}

httpd_handle_t webserver_get_handle(void) {
    return server;
}

void webserver_register_uri(httpd_uri_t *uri) {
    if (server && uri) {
        httpd_register_uri_handler(server, uri);
    }
}