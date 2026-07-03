// modbus/modbus_tcp.c
// Lightweight MODBUS TCP slave implementation

#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

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

// ============================================================
// Constants
// ============================================================
#define MODBUS_PORT 502
#define MAX_REGISTERS 1024
#define MAX_CLIENTS 1
#define RECEIVE_TIMEOUT_MS 100

// ============================================================
// Static Variables
// ============================================================
static uint16_t registers[MAX_REGISTERS] = {0};
static SemaphoreHandle_t modbus_mutex = NULL;
static int server_socket = -1;
static int modbus_running = 0;
static uint32_t packet_count = 0;
static uint32_t error_count = 0;
static uint16_t last_command = 0;
static uint16_t heartbeat_counter = 0;

// ============================================================
// Internal Functions
// ============================================================

// Handle read holding registers (0x03)
static int handle_read_registers(uint8_t *request, int len, uint8_t *response) {
    if (len < 12) return -1;
    
    uint16_t start_addr = (request[8] << 8) | request[9];
    uint16_t count = (request[10] << 8) | request[11];
    
    if (start_addr + count > MAX_REGISTERS) {
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_ADDRESS;
        return 9;
    }
    
    response[0] = request[0];
    response[1] = request[1];
    response[2] = request[2];
    response[3] = request[3];
    response[4] = (count * 2 + 3) >> 8;
    response[5] = (count * 2 + 3) & 0xFF;
    response[6] = request[6];
    response[7] = request[7];
    response[8] = count * 2;
    
    int idx = 9;
    for (int i = 0; i < count; i++) {
        uint16_t addr = start_addr + i;
        uint16_t value = 0;
        if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            value = registers[addr];
            xSemaphoreGive(modbus_mutex);
        }
        response[idx++] = value >> 8;
        response[idx++] = value & 0xFF;
    }
    
    return idx;
}

// Handle write single register (0x06)
static int handle_write_register(uint8_t *request, int len, uint8_t *response) {
    if (len < 12) return -1;
    
    uint16_t addr = (request[8] << 8) | request[9];
    uint16_t value = (request[10] << 8) | request[11];
    
    if (addr >= MAX_REGISTERS) {
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

// Handle write multiple registers (0x10)
static int handle_write_multiple_registers(uint8_t *request, int len, uint8_t *response) {
    if (len < 13) return -1;
    
    uint16_t start_addr = (request[8] << 8) | request[9];
    uint16_t count = (request[10] << 8) | request[11];
    // byte_count not used - suppress warning
    (void)request[12];
    
    if (start_addr + count > MAX_REGISTERS) {
        response[7] = request[7] | 0x80;
        response[8] = MB_EX_ILLEGAL_ADDRESS;
        return 9;
    }
    
    int idx = 13;
    for (int i = 0; i < count; i++) {
        if (idx + 1 >= len) break;
        uint16_t addr = start_addr + i;
        uint16_t value = (request[idx] << 8) | request[idx + 1];
        idx += 2;
        modbus_write_register(addr, value);
    }
    
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
        return -1;
    }
    
    uint8_t func_code = request[7];
    int response_len = 0;
    
    switch (func_code) {
        case MB_FC_READ_HOLDING:
            response_len = handle_read_registers(request, len, response);
            break;
        case MB_FC_WRITE_SINGLE:
            response_len = handle_write_register(request, len, response);
            break;
        case MB_FC_WRITE_MULTIPLE:
            response_len = handle_write_multiple_registers(request, len, response);
            break;
        default:
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
    return response_len;
}

// ============================================================
// MODBUS Server Task
// ============================================================
static void modbus_server_task(void *pvParameters) {
    APP_LOG_I("MODBUS server task started on port %d", MODBUS_PORT);
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        APP_LOG_E("Failed to create MODBUS socket");
        return;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(MODBUS_PORT),
        .sin_addr = { .s_addr = INADDR_ANY }
    };
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        APP_LOG_E("MODBUS bind failed");
        close(server_socket);
        return;
    }
    
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        APP_LOG_E("MODBUS listen failed");
        close(server_socket);
        return;
    }
    
    modbus_running = 1;
    APP_LOG_I("MODBUS server listening on port %d", MODBUS_PORT);
    
    while (modbus_running) {
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        int activity = select(server_socket + 1, &read_fds, NULL, NULL, &tv);
        if (activity < 0) {
            continue;
        }
        if (activity == 0) {
            goto heartbeat;
        }
        
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) continue;
        
        struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        uint8_t request[256];
        int len = recv(client_sock, request, sizeof(request), 0);
        
        if (len > 0) {
            uint8_t response[256];
            int response_len = process_request(request, len, response);
            if (response_len > 0) {
                send(client_sock, response, response_len, 0);
            }
        }
        
        close(client_sock);
        
    heartbeat:
        heartbeat_counter = (heartbeat_counter + 1) & 0xFFFF;
        
        if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (REG_HEARTBEAT < MAX_REGISTERS) {
                registers[REG_HEARTBEAT] = heartbeat_counter;
            }
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
        
        watchdog_heartbeat(WDT_MODULE_MODBUS);
        
        uint32_t interval = nvs_get_modbus_interval();
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
    
    close(server_socket);
    server_socket = -1;
    modbus_running = 0;
    APP_LOG_I("MODBUS server stopped");
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
    
    APP_LOG_I("MODBUS initialized");
}

void modbus_start(void) {
    modbus_running = 1;
}

void modbus_stop(void) {
    modbus_running = 0;
}

void modbus_update_register(uint16_t addr, uint16_t value) {
    if (addr >= MAX_REGISTERS) return;
    
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        registers[addr] = value;
        xSemaphoreGive(modbus_mutex);
    }
}

uint16_t modbus_read_register(uint16_t addr) {
    if (addr >= MAX_REGISTERS) return 0;
    
    uint16_t value = 0;
    if (modbus_mutex && xSemaphoreTake(modbus_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        value = registers[addr];
        xSemaphoreGive(modbus_mutex);
    }
    return value;
}

int modbus_write_register(uint16_t addr, uint16_t value) {
    if (addr >= MAX_REGISTERS) return -1;
    
    // Validate and handle special registers
    if (addr >= REG_RELAY_BASE && addr < REG_RELAY_BASE + 12) {
        // Relay control register
        uint8_t relay_id = addr - REG_RELAY_BASE;
        if (value) {
            // Trigger relay
            // relay_trigger(relay_id);  // Uncomment when relay module is integrated
        } else {
            // Turn off relay
            // relay_off(relay_id);
        }
        APP_LOG_I("MODBUS: Relay %d %s", relay_id, value ? "ON" : "OFF");
    }
    
    if (addr == REG_COMMAND) {
        // Command register
        switch (value) {
            case CMD_READ_SENSORS:
                // Force sensor read
                // sensor_poll_all();
                APP_LOG_I("MODBUS: Command - Read sensors");
                break;
            case CMD_CLEAR_ALERTS:
                APP_LOG_I("MODBUS: Command - Clear alerts");
                break;
            case CMD_RESET_SYSTEM:
                APP_LOG_I("MODBUS: Command - System reset");
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
