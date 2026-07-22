// modbus/modbus_tcp.c
// Lightweight MODBUS TCP slave implementation with FLOAT support

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"

#include "modbus_tcp.h"
#include "modbus_map.h"
#include "log_levels.h"
#include "logger.h"
#include "system_config.h"
#include "watchdog.h"
#include "nvs_config.h"
#include "relay_control.h"
#include "sensor_read.h"

// ============================================================
// Constants
// ============================================================
#define MODBUS_PORT 502
#define MAX_REGISTERS 256
#define MAX_CLIENTS 1
#define RECEIVE_TIMEOUT_MS 100
#define CLIENT_TIMEOUT_SEC 5

#define MODBUS_OFFSET_HOLDING   40000   // Holding registers start at 40000 in PLC notation
#define MODBUS_OFFSET_INPUT     30000   // Input registers start at 30000
#define MODBUS_OFFSET_COIL      00000   // Coils start at 00000

// Modbus exception codes
#define MB_EX_ILLEGAL_FUNCTION  0x01
#define MB_EX_ILLEGAL_ADDRESS   0x02
#define MB_EX_ILLEGAL_DATA_VALUE 0x03
#define MB_EX_SLAVE_FAILURE     0x04

// ============================================================
// Static Variables
// ============================================================
static uint16_t registers[MAX_REGISTERS] = {0};
static SemaphoreHandle_t modbus_mutex = NULL;
static int server_socket = -1;
static volatile int modbus_running = 0;
static uint32_t packet_count = 0;
static uint32_t error_count = 0;
static uint16_t last_command = 0;
static uint16_t heartbeat_counter = 0;
static uint8_t modbus_addressing_mode = MODBUS_MAP_MODE_AUTO;

// ============================================================
// Float Conversion Helpers
// ============================================================

/**
 * Store a float value in the register array at the specified address
 * Address must be even-aligned (multiple of 2)
 */
static void store_float_in_registers(uint16_t addr, float value) {
    if (addr >= MAX_REGISTERS - 1) {
        APP_LOG_E("MODBUS: Invalid address for float storage: %d", addr);
        return;
    }

    uint32_t float_bits;
    memcpy(&float_bits, &value, sizeof(float_bits));

    registers[addr] = (uint16_t)(float_bits >> 16);
    registers[addr + 1] = (uint16_t)(float_bits & 0xFFFF);
}

/**
 * Read a float value from the register array at the specified address
 * Address must be even-aligned (multiple of 2)
 */
static float read_float_from_registers(uint16_t addr) {
    if (addr >= MAX_REGISTERS - 1) {
        APP_LOG_E("MODBUS: Invalid address for float read: %d", addr);
        return 0.0f;
    }
    
    uint32_t float_bits = 0;
    float_bits |= ((uint32_t)registers[addr] << 16);
    float_bits |= registers[addr + 1];
    
    float value;
    memcpy(&value, &float_bits, sizeof(value));
    return value;
}

// ============================================================
// Internal Functions
// ============================================================

// Configuration function
void modbus_set_addressing_mode(uint8_t mode) {
    modbus_addressing_mode = mode;
    APP_LOG_D("MODBUS: Addressing mode set to %d", mode);
}

// Debug function to hex dump packets
static void debug_hex_dump(const char *tag, const uint8_t *data, int len, const char *direction) {
    if (len <= 0) return;
    
    char hex_str[512] = {0};
    char ascii_str[64] = {0};
    int offset = 0;
    
    for (int i = 0; i < len && offset < (int)sizeof(hex_str) - 4; i++) {
        offset += snprintf(hex_str + offset, sizeof(hex_str) - offset, "%02X ", data[i]);
        if (isprint(data[i])) {
            ascii_str[i % 32] = data[i];
        } else {
            ascii_str[i % 32] = '.';
        }
        if ((i + 1) % 16 == 0 || i == len - 1) {
            ascii_str[(i % 32) + 1] = '\0';
            APP_LOG_D("%s %s (%d bytes): %s  |%s|", tag, direction, len, hex_str, ascii_str);
            hex_str[0] = '\0';
            ascii_str[0] = '\0';
            offset = 0;
        }
    }
}

// Address translation function with mode support
static uint16_t translate_plc_to_raw(uint16_t plc_address, uint8_t func_code) {
    uint16_t raw_addr = plc_address;
    
    switch (func_code) {
        case MB_FC_READ_HOLDING_REGISTERS:   // 0x03
        case MB_FC_WRITE_SINGLE_REGISTER:    // 0x06
        case MB_FC_WRITE_MULTIPLE_REGISTERS: // 0x10
            // 40001 -> 0, 40002 -> 1, etc.
            if (plc_address >= MODBUS_OFFSET_HOLDING) {
                raw_addr = plc_address - MODBUS_OFFSET_HOLDING;
            }
            break;
            
        case MB_FC_READ_INPUT_REGISTERS:     // 0x04
            // 30001 -> 0, 30002 -> 1, etc.
            if (plc_address >= MODBUS_OFFSET_INPUT) {
                raw_addr = plc_address - MODBUS_OFFSET_INPUT;
            }
            break;
            
        case MB_FC_READ_COILS:               // 0x01
        case MB_FC_WRITE_SINGLE_COIL:        // 0x05
            // 00001 -> 0, 00002 -> 1, etc.
            raw_addr = plc_address - MODBUS_OFFSET_COIL;
            break;
            
        default:
            // For unsupported function codes, assume raw addressing
            APP_LOG_W("MODBUS: Unknown function code 0x%02X for address translation", func_code);
            break;
    }
    
    return raw_addr;
}

// Handle read holding registers (0x03) - SUPPORTS FLOAT
static int handle_read_registers(uint8_t *request, int len, uint8_t *response) {
    if (len < 12) {
        APP_LOG_E("handle_read_registers: len=%d too short", len);
        return -1;
    }
    
    uint16_t plc_start_addr = (request[8] << 8) | request[9];
    uint16_t count = (request[10] << 8) | request[11];
    
    // Translate from PLC addressing (40001-based) to raw addressing
    uint16_t start_addr = translate_plc_to_raw(plc_start_addr, MB_FC_READ_HOLDING);
    
    APP_LOG_D("MODBUS: PLC read: 4%05d (raw: %d), count=%d", 
              plc_start_addr + MODBUS_OFFSET_HOLDING, start_addr, count);
    
    if (start_addr + count > MAX_REGISTERS) {
        APP_LOG_E("MODBUS: Address out of range: raw=%d, max=%d", start_addr, MAX_REGISTERS);
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_ADDRESS;
        return 9;
    }
    
    bool is_float = start_addr < (MODBUS_SENSOR_BASE + TOTAL_SENSOR_COUNT); // Sensors are always float, the rest are S16

    response[0] = request[0];  // Transaction ID
    response[1] = request[1];
    response[2] = request[2];  // Protocol ID
    response[3] = request[3];
    response[4] = (count * 2 + 3) >> 8;   // Length high byte
    response[5] = (count * 2 + 3) & 0xFF; // Length low byte
    response[6] = request[6];  // Unit ID
    response[7] = request[7];  // Function code
    response[8] = count * 2;   // Byte count
    
    int idx = 9;
    
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (is_float) {
            // Reading float values (2 registers per float)
            int float_count = count;
            APP_LOG_D("MODBUS: Reading %d float values starting at raw addr %d", float_count, start_addr);
            
            for (int i = 0; i < float_count; i++) {
                uint16_t addr = start_addr + i; // ( i * 2 ) 
                float value = read_float_from_registers(addr);
                
                // Store the float bytes in response (Big-Endian)
                uint32_t float_bits;
                memcpy(&float_bits, &value, sizeof(float_bits));
                response[idx++] = (float_bits >> 24) & 0xFF;
                response[idx++] = (float_bits >> 16) & 0xFF;
                response[idx++] = (float_bits >> 8) & 0xFF;
                response[idx++] = float_bits & 0xFF;
                
                if (i < 10) {
                    APP_LOG_D("MODBUS: float[%d] @ addr %d = %f (0x%08X)", i, addr, value, float_bits);
                }
            }
        } else {
            // Reading S16 values (original behavior)
            APP_LOG_D("MODBUS: Reading %d S16 values starting at raw addr %d", count, start_addr);
            
            for (int i = 0; i < count; i++) {
                uint16_t addr = start_addr + i;
                uint16_t value = registers[addr];
                response[idx++] = value >> 8;
                response[idx++] = value & 0xFF;
                
                if (i < 10) {
                    APP_LOG_D("MODBUS: reg[%d] = %d (0x%04X)", addr, value, value);
                }
            }
        }
        xSemaphoreGive(modbus_mutex);
    }
    
    return idx;
}

// Handle write single register (0x06) - SUPPORTS FLOAT
static int handle_write_register(uint8_t *request, int len, uint8_t *response) {
    if (len < 12) return -1;
    
    uint16_t plc_addr = (request[8] << 8) | request[9];
    uint16_t value = (request[10] << 8) | request[11];
    
    // Translate from PLC addressing to raw addressing
    uint16_t addr = translate_plc_to_raw(plc_addr, MB_FC_WRITE_SINGLE);
    
    APP_LOG_D("MODBUS: PLC write: 4%05d (raw: %d) = %d (0x%04X)", 
              plc_addr + MODBUS_OFFSET_HOLDING, addr, value, value);
    
    if (addr >= MAX_REGISTERS) {
        APP_LOG_E("MODBUS: Address out of range: raw=%d", addr);
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_ADDRESS;
        return 9;
    }
    
    int result = modbus_write_register(addr, value);
    if (result < 0) {
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_SLAVE_FAILURE;
        return 9;
    }
    
    memcpy(response, request, 12);
    return 12;
}

// Handle write multiple registers (0x10) - SUPPORTS FLOAT
static int handle_write_multiple_registers(uint8_t *request, int len, uint8_t *response) {
    if (len < 13) return -1;
    
    uint16_t plc_start_addr = (request[8] << 8) | request[9];
    uint16_t count = (request[10] << 8) | request[11];
    uint8_t byte_count = request[12];
    
    // Translate from PLC addressing to raw addressing
    uint16_t start_addr = translate_plc_to_raw(plc_start_addr, MB_FC_WRITE_MULTIPLE);
    
    APP_LOG_D("MODBUS: PLC write multiple: 4%05d (raw: %d), count=%d, bytes=%d", 
              plc_start_addr + MODBUS_OFFSET_HOLDING, start_addr, count, byte_count);

    if (start_addr + count > MAX_REGISTERS) {
        APP_LOG_E("MODBUS: Address out of range: raw=%d", start_addr);
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_ADDRESS;
        return 9;
    }
    
    // Validate byte count
    if (byte_count != count * 2) {
        APP_LOG_W("MODBUS: Byte count mismatch: expected %d, got %d", count * 2, byte_count);
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_DATA_VALUE;
        return 9;
    }
    
    // Check if we're writing float values (even count, even start address)
    bool is_float = (start_addr + count) < (MODBUS_SENSOR_BASE + TOTAL_SENSOR_COUNT) ;
    
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (is_float) {
            // Writing float values (2 registers per float)
            int float_count = count / 2;
            APP_LOG_D("MODBUS: Writing %d float values starting at raw addr %d", float_count, start_addr);
            
            int idx = 13;
            for (int i = 0; i < float_count; i++) {
                if (idx + 3 >= len) break;
                
                uint16_t addr = start_addr + i;
                
                // Read the 4 bytes (2 registers) as a float (Big-Endian)
                uint32_t float_bits = 0;
                float_bits |= ((uint32_t)request[idx] << 24);
                float_bits |= ((uint32_t)request[idx + 1] << 16);
                float_bits |= ((uint32_t)request[idx + 2] << 8);
                float_bits |= request[idx + 3];
                
                float value;
                memcpy(&value, &float_bits, sizeof(value));
                idx += 4;
                
                // Store as two 16-bit registers
                registers[addr] = (uint16_t)(float_bits >> 16);
                registers[addr + 1] = (uint16_t)(float_bits & 0xFFFF);
                
                APP_LOG_D("MODBUS: Wrote float[%d] = %f to addr %d (0x%08X)", i, value, addr, float_bits);
            }
        } else {
            // Writing S16 values (original behavior)
            APP_LOG_D("MODBUS: Writing %d S16 values starting at raw addr %d", count, start_addr);
            
            int idx = 13;
            for (int i = 0; i < count; i++) {
                if (idx + 1 >= len) break;
                uint16_t addr = start_addr + i;
                uint16_t value = (request[idx] << 8) | request[idx + 1];
                idx += 2;
                
                // Check for special registers
                if (addr >= MODBUS_RELAY_BASE && addr < MODBUS_RELAY_BASE + 12) {
                    uint8_t relay_id = addr - MODBUS_RELAY_BASE;
                    if (value) {
                        relay_trigger(relay_id);
                    } else {
                        relay_off(relay_id);
                    }
                    APP_LOG_D("MODBUS: Relay %d %s", relay_id, value ? "ON" : "OFF");
                }
                
                if (addr == MODBUS_COMMAND_REG) {
                    switch (value) {
                        case CMD_READ_SENSORS:
                            APP_LOG_D("MODBUS: Command - Read sensors");
                            break;
                        case CMD_CLEAR_ALERTS:
                            APP_LOG_D("MODBUS: Command - Clear alerts");
                            break;
                        case CMD_RESET_SYSTEM:
                            APP_LOG_D("MODBUS: Command - System reset");
                            esp_restart();
                            break;
                        default:
                            APP_LOG_W("MODBUS: Unknown command: %d", value);
                            break;
                    }
                }
                
                registers[addr] = value;
                APP_LOG_D("MODBUS: Wrote S16[%d] = %d to addr %d", i, value, addr);
            }
        }
        xSemaphoreGive(modbus_mutex);
    }
    
    // Echo back the request header
    response[0] = request[0];
    response[1] = request[1];
    response[2] = request[2];
    response[3] = request[3];
    response[4] = 0x00;
    response[5] = 0x06;
    response[6] = request[6];
    response[7] = request[7];
    response[8] = request[8];
    response[9] = request[9];
    response[10] = request[10];
    response[11] = request[11];
    
    return 12;
}

// Process a MODBUS request
static int process_request(uint8_t *request, int len, uint8_t *response) {
    if (len < 8) {
        error_count++;
        APP_LOG_E("MODBUS: Request too short: %d bytes", len);
        return -1;
    }
    
    // DEBUG: Log incoming request
    debug_hex_dump("MODBUS", request, len, "RX");
    
    // Parse MBAP header
    uint16_t transaction_id = (request[0] << 8) | request[1];
    uint16_t protocol_id = (request[2] << 8) | request[3];
    uint16_t frame_length = (request[4] << 8) | request[5];
    uint8_t unit_id = request[6];
    uint8_t func_code = request[7];
    
    APP_LOG_D("MODBUS: TID=%d, Protocol=%d, Len=%d, Unit=%d, FC=0x%02X", 
              transaction_id, protocol_id, frame_length, unit_id, func_code);
    
    int response_len = 0;
    
    switch (func_code) {
        case MB_FC_READ_HOLDING_REGISTERS:   // 0x03
        case MB_FC_READ_INPUT_REGISTERS:     // 0x04
            if (len < 12) {
                APP_LOG_E("MODBUS: Read request too short");
                return -1;
            }
            uint16_t start_addr = (request[8] << 8) | request[9];
            uint16_t count = (request[10] << 8) | request[11];
            APP_LOG_D("MODBUS: Read %s Registers: addr=%d, count=%d", 
                     func_code == MB_FC_READ_HOLDING_REGISTERS ? "Holding" : "Input", 
                     start_addr, count);
            
            response_len = handle_read_registers(request, len, response);
            break;
            
        case MB_FC_WRITE_SINGLE_REGISTER:    // 0x06
        {
            uint16_t addr = (request[8] << 8) | request[9];
            uint16_t value = (request[10] << 8) | request[11];
            APP_LOG_D("MODBUS: Write Single: addr=%d, value=%d (0x%04X)", addr, value, value);
            response_len = handle_write_register(request, len, response);
            break;
        }
            
        case MB_FC_WRITE_MULTIPLE_REGISTERS: // 0x10
            start_addr = (request[8] << 8) | request[9];
            count = (request[10] << 8) | request[11];
            APP_LOG_D("MODBUS: Write Multiple: addr=%d, count=%d", start_addr, count);
            response_len = handle_write_multiple_registers(request, len, response);
            break;
            
        case MB_FC_READ_COILS:               // 0x01
        case MB_FC_WRITE_SINGLE_COIL:        // 0x05
            APP_LOG_W("MODBUS: Coil operations not implemented yet");
            response[7] = func_code | 0x80;
            response[8] = MB_EX_ILLEGAL_FUNCTION;
            response_len = 9;
            break;
            
        default:
            APP_LOG_W("MODBUS: Unsupported function code: 0x%02X", func_code);
            response[0] = request[0];
            response[1] = request[1];
            response[2] = request[2];
            response[3] = request[3];
            response[4] = 0x00;
            response[5] = 0x03;
            response[6] = request[6];
            response[7] = func_code | 0x80;
            response[8] = MB_EX_ILLEGAL_FUNCTION;
            response_len = 9;
            error_count++;
            break;
    }
    
    packet_count++;
    last_command = func_code;
    
    // DEBUG: Log outgoing response
    if (response_len > 0) {
        debug_hex_dump("MODBUS", response, response_len, "TX");
    }
    
    return response_len;
}

// ============================================================
// Connection Management Functions
// ============================================================

// Set socket to non-blocking mode
static int set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        APP_LOG_E("MODBUS: fcntl F_GETFL failed: errno %d", errno);
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) == -1) {
        APP_LOG_E("MODBUS: fcntl F_SETFL failed: errno %d", errno);
        return -1;
    }
    return 0;
}

// Clean up client socket properly
static void cleanup_client_socket(int *client_sock) {
    if (client_sock && *client_sock >= 0) {
        shutdown(*client_sock, SHUT_RDWR);
        close(*client_sock);
        *client_sock = -1;
        APP_LOG_D("MODBUS: Client socket cleaned up");
    }
}

// Clean up server socket properly
static void cleanup_server_socket(void) {
    if (server_socket >= 0) {
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
        server_socket = -1;
        APP_LOG_D("MODBUS: Server socket cleaned up");
    }
}

// ============================================================
// MODBUS Server Task
// ============================================================
static void modbus_server_task(void *pvParameters) {
    APP_LOG_D("MODBUS server task started on port %d", MODBUS_PORT);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        APP_LOG_E("Failed to create MODBUS socket: errno %d", errno);
        return;
    }
    
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        APP_LOG_W("MODBUS: setsockopt SO_REUSEADDR failed: errno %d", errno);
    }
    
    if (set_non_blocking(server_socket) < 0) {
        APP_LOG_E("MODBUS: Failed to set non-blocking mode");
        cleanup_server_socket();
        return;
    }
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MODBUS_PORT),
        .sin_addr = { .s_addr = INADDR_ANY }
    };
    
    int bind_retry = 0;
    while (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0 && bind_retry < 5) {
        APP_LOG_E("MODBUS bind failed (attempt %d): errno %d", bind_retry + 1, errno);
        vTaskDelay(pdMS_TO_TICKS(1000));
        bind_retry++;
    }
    
    if (bind_retry >= 5) {
        APP_LOG_E("MODBUS bind failed after 5 attempts");
        cleanup_server_socket();
        return;
    }
    
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        APP_LOG_E("MODBUS listen failed: errno %d", errno);
        cleanup_server_socket();
        return;
    }
    
    modbus_running = 1;
    APP_LOG_D("MODBUS server listening on port %d", MODBUS_PORT);
    
    while (modbus_running) {
        // Use select with a short timeout to check modbus_running periodically
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        int activity = select(server_socket + 1, &read_fds, NULL, NULL, &tv);
        if (activity < 0) {
            if (errno != EINTR) {
                APP_LOG_E("MODBUS select error: errno %d", errno);
            }
            continue;
        }
        
        if (activity > 0 && FD_ISSET(server_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_sock < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    APP_LOG_E("MODBUS client accept failed: errno %d", errno);
                }
                continue;
            }
            
            // Set non-blocking for client socket
            if (set_non_blocking(client_sock) < 0) {
                APP_LOG_E("MODBUS: Failed to set client non-blocking");
                cleanup_client_socket(&client_sock);
                continue;
            }
            
            // Set keep-alive to detect dead connections
            int keepalive = 1;
            if (setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0) {
                APP_LOG_W("MODBUS: setsockopt SO_KEEPALIVE failed: errno %d", errno);
            }
            
            // Set receive timeout
            struct timeval timeout = { .tv_sec = CLIENT_TIMEOUT_SEC, .tv_usec = 0 };
            if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                APP_LOG_W("MODBUS: setsockopt SO_RCVTIMEO failed: errno %d", errno);
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            APP_LOG_D("MODBUS client connected from %s:%d", client_ip, ntohs(client_addr.sin_port));
            
            bool client_connected = true;
            int idle_count = 0;
            const int max_idle = 10;
            
            while (modbus_running && client_connected) {
                // Use select for client socket with timeout
                fd_set client_fds;
                FD_ZERO(&client_fds);
                FD_SET(client_sock, &client_fds);
                
                struct timeval client_tv = { .tv_sec = 0, .tv_usec = 100000 }; // 100ms
                int client_activity = select(client_sock + 1, &client_fds, NULL, NULL, &client_tv);
                
                if (client_activity < 0) {
                    if (errno != EINTR) {
                        APP_LOG_E("MODBUS client select error: errno %d", errno);
                        client_connected = false;
                        cleanup_client_socket(&client_sock);
                        break;
                    }
                    continue;
                }
                
                if (client_activity == 0) {
                    idle_count++;
                    if (idle_count > max_idle) {
                        idle_count = 0;
                    }
                    
                    // Update heartbeat registers periodically
                    heartbeat_counter = (heartbeat_counter + 1) & 0xFFFF;
                    if (heartbeat_counter % 10 == 0) {
                        if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                            if (MODBUS_HEARTBEAT_REG < MAX_REGISTERS) {
                                registers[MODBUS_HEARTBEAT_REG] = heartbeat_counter;
                            }
                            xSemaphoreGive(modbus_mutex);
                        }
                    }
                    
                    watchdog_heartbeat(WDT_MODULE_MODBUS);
                    continue;
                }
                
                uint8_t request[MAX_REGISTERS];
                int len = recv(client_sock, request, sizeof(request), 0);
                
                if (len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    } else if (errno == ECONNRESET || errno == EPIPE) {
                        APP_LOG_W("MODBUS: Connection reset by peer");
                        client_connected = false;
                        break;
                    } else {
                        APP_LOG_E("MODBUS recv error: errno %d", errno);
                        client_connected = false;
                        break;
                    }
                }
                
                if (len == 0) {
                    APP_LOG_D("MODBUS: Client disconnected gracefully");
                    client_connected = false;
                    break;
                }
                
                if (len < 8) {
                    APP_LOG_W("MODBUS: Incomplete frame (%d bytes)", len);
                    continue;
                }
                
                // Update sensor registers (FLOAT values)
                if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    for (int i = 0; i < TOTAL_SENSOR_COUNT; i++) {
                        float sensor_value = sensor_get_reading(i)->value;
                        // Store float at even-aligned addresses
                        store_float_in_registers(MODBUS_SENSOR_BASE + i, sensor_value);
                    }
                    
                    for (int i = 0; i < RELAY_COUNT; i++) {
                        uint16_t relay_state = relay_get_state(i);
                        registers[MODBUS_RELAY_BASE + i] = relay_state;
                    }
                    xSemaphoreGive(modbus_mutex);
                }
                
                uint8_t response[MAX_REGISTERS];
                int response_len = process_request(request, len, response);
                
                if (response_len > 0) {
                    int sent = send(client_sock, response, response_len, 0);
                    if (sent < 0) {
                        if (errno == EPIPE || errno == ECONNRESET) {
                            APP_LOG_W("MODBUS: Connection broken during send");
                            client_connected = false;
                            break;
                        } else {
                            APP_LOG_E("MODBUS send failed: errno %d", errno);
                            continue;
                        }
                    }
                    APP_LOG_D("MODBUS sent %d bytes response", sent);
                }
            }
            
            cleanup_client_socket(&client_sock);
            APP_LOG_D("MODBUS: Client connection closed");
        }
        
        // Update statistics periodically
        uint32_t interval = nvs_get_modbus_interval();
        if (interval > 0 && heartbeat_counter % 100 == 0) {
            if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (REG_PACKET_LOW < MAX_REGISTERS) {
                    registers[REG_PACKET_LOW] = packet_count & 0xFFFF;
                }
                if (REG_PACKET_HIGH < MAX_REGISTERS) {
                    registers[REG_PACKET_HIGH] = (packet_count >> 16) & 0xFFFF;
                }
                if (REG_ERROR_LOW < MAX_REGISTERS) {
                    registers[REG_ERROR_LOW] = error_count & 0xFFFF;
                }
                if (REG_ERROR_HIGH < MAX_REGISTERS) {
                    registers[REG_ERROR_HIGH] = (error_count >> 16) & 0xFFFF;
                }
                if (REG_LAST_CMD < MAX_REGISTERS) {
                    registers[REG_LAST_CMD] = last_command;
                }
                xSemaphoreGive(modbus_mutex);
            }
        }
        
        watchdog_heartbeat(WDT_MODULE_MODBUS);
    }
    
    cleanup_server_socket();
    modbus_running = 0;
    APP_LOG_D("MODBUS server stopped");
}

// ============================================================
// Public Functions
// ============================================================

void modbus_init(void) {
    modbus_mutex = xSemaphoreCreateMutex();
    if (!modbus_mutex) {
        APP_LOG_E("Failed to create MODBUS mutex");
        return;
    }
    
    memset(registers, 0, sizeof(registers));
    
    xTaskCreate(modbus_server_task, "modbus", STACK_SIZE_MODBUS, NULL, PRIORITY_MODBUS, NULL);
    
    watchdog_register_module(WDT_MODULE_MODBUS, 10);
    
    APP_LOG_I("MODBUS initialized with FLOAT support");
}

void modbus_start(void) {
    modbus_running = 1;
}

void modbus_stop(void) {
    modbus_running = 0;
}

void modbus_update_register(uint16_t addr, float value) {
    if (addr >= MAX_REGISTERS) return;
    
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Store float as raw bits in two registers
        if (addr < (MODBUS_SENSOR_BASE + TOTAL_SENSOR_COUNT)) {
            store_float_in_registers(addr, value);
            APP_LOG_D("MODBUS: Updated float reg[%d-%d] = %f", addr, addr+1, value);
        } else {
            // If not properly aligned, just store as integer (backward compatible)
            registers[addr] = (uint16_t)value;
            APP_LOG_W("MODBUS: Warning - storing float as int at odd address %d", addr);
        }
        xSemaphoreGive(modbus_mutex);
    }
}

float modbus_read_float_register(uint16_t addr) {
    if (addr >= MAX_REGISTERS - 1) return 0.0f;
    
    float value = 0.0f;
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (addr < (MODBUS_SENSOR_BASE + TOTAL_SENSOR_COUNT)) {
            value = read_float_from_registers(addr);
        } else {
            // Read as integer and convert to float
            value = (float)registers[addr];
            APP_LOG_W("MODBUS: Reading float from odd address %d - treating as int", addr);
        }
        xSemaphoreGive(modbus_mutex);
    }
    return value;
}

uint16_t modbus_read_register(uint16_t addr) {
    APP_LOG_D("MODBUS read register %d", addr);
    if (addr >= MAX_REGISTERS) return 0;
    
    uint16_t value = 0;
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        value = registers[addr];
        xSemaphoreGive(modbus_mutex);
    }
    APP_LOG_D("MODBUS read register %d, %d", addr, value);
    return value;
}

int modbus_write_register(uint16_t addr, uint16_t value) {
    if (addr >= MAX_REGISTERS) return -1;
    
    // Validate and handle special registers
    if (addr >= MODBUS_RELAY_BASE && addr < MODBUS_RELAY_BASE + RELAY_COUNT) {
        // Relay control register
        uint8_t relay_id = addr - MODBUS_RELAY_BASE;
        if (value) {
            relay_trigger(relay_id);
        } else {
            relay_off(relay_id);
        }
        APP_LOG_D("MODBUS: Relay %d %s", relay_id, value ? "ON" : "OFF");
    }
    
    if (addr == MODBUS_COMMAND_REG) {
        // Command register
        switch (value) {
            case CMD_READ_SENSORS:
                APP_LOG_D("MODBUS: Command - Read sensors");
                break;
            case CMD_CLEAR_ALERTS:
                APP_LOG_D("MODBUS: Command - Clear alerts");
                break;
            case CMD_RESET_SYSTEM:
                APP_LOG_D("MODBUS: Command - System reset");
                esp_restart();
                break;
            default:
                APP_LOG_W("MODBUS: Unknown command: %d", value);
                break;
        }
    }
    
    // Write register
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
		registers[addr] = value;
        xSemaphoreGive(modbus_mutex);
    }
    
    return 0;
}

void modbus_get_status(uint32_t *pkt_count, uint32_t *err_count) {
    if (pkt_count) *pkt_count = packet_count;
    if (err_count) *err_count = error_count;
}