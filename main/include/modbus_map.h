// modbus_map.h
// MODBUS register map definitions - lightweight (128 registers)

#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#include <stdint.h>
#include "system_config.h"

// ============================================================
// System Registers (0x0200 - 0x020F)
// ============================================================
#define REG_UPTIME_LOW      0x0200
#define REG_UPTIME_HIGH     0x0201
#define REG_VERSION         0x0202
#define REG_HEAP_LOW        0x0203
#define REG_HEAP_HIGH       0x0204
#define REG_WIFI_RSSI       0x0205
#define REG_NTP_STATUS      0x0206
#define REG_WIFI_MODE       0x0207

// ============================================================
// Status Registers (0x0300 - 0x030F)
// ============================================================
#define REG_HEARTBEAT       0x0300
#define REG_PACKET_LOW      0x0301
#define REG_PACKET_HIGH     0x0302
#define REG_ERROR_LOW       0x0303
#define REG_ERROR_HIGH      0x0304
#define REG_LAST_CMD        0x0305

// ============================================================
// Command Register (0x0310)
// ============================================================
#define REG_COMMAND         0x0310
#define CMD_READ_SENSORS    1
#define CMD_CLEAR_ALERTS    2
#define CMD_RESET_SYSTEM    3

// ============================================================
// MODBUS Exception Codes
// ============================================================
#define MB_EX_ILLEGAL_FUNCTION   0x01
#define MB_EX_ILLEGAL_ADDRESS    0x02
#define MB_EX_ILLEGAL_VALUE      0x03
#define MB_EX_SLAVE_FAILURE      0x04
#define MB_EX_ACKNOWLEDGE        0x05
#define MB_EX_BUSY               0x06

// ============================================================
// Mapping standards
// ============================================================
#define MODBUS_MAP_MODE_RAW      0  // Use raw addressing (0-65535)
#define MODBUS_MAP_MODE_PLC      1  // Use PLC addressing (40001-49999)
#define MODBUS_MAP_MODE_AUTO     2  // Auto-detect from request

// ============================================================
// MODBUS Function Codes
// ============================================================
#define MB_FC_READ_COILS                0x01
#define MB_FC_READ_DISCRETE_INPUTS      0x02
#define MB_FC_READ_HOLDING_REGISTERS    0x03
#define MB_FC_READ_INPUT_REGISTERS      0x04
#define MB_FC_WRITE_SINGLE_COIL         0x05
#define MB_FC_WRITE_SINGLE_REGISTER     0x06
#define MB_FC_WRITE_MULTIPLE_COILS      0x0F
#define MB_FC_WRITE_MULTIPLE_REGISTERS  0x10
#define MB_FC_READ_FILE_RECORD          0x14
#define MB_FC_WRITE_FILE_RECORD         0x15
#define MB_FC_MASK_WRITE_REGISTER       0x16
#define MB_FC_READ_WRITE_MULTIPLE_REGS  0x17

// ============================================================
// MODBUS Exception Codes
// ============================================================
#define MB_EX_ILLEGAL_FUNCTION          0x01
#define MB_EX_ILLEGAL_ADDRESS           0x02
#define MB_EX_ILLEGAL_DATA_VALUE        0x03
#define MB_EX_SLAVE_FAILURE             0x04
#define MB_EX_ACKNOWLEDGE               0x05
#define MB_EX_SLAVE_BUSY                0x06
#define MB_EX_MEMORY_PARITY_ERROR       0x08
#define MB_EX_GATEWAY_PATH_UNAVAILABLE  0x0A
#define MB_EX_GATEWAY_TARGET_FAILED     0x0B

// ============================================================
// Aliases for backward compatibility (if your code uses these)
// ============================================================
#define MB_FC_READ_HOLDING              MB_FC_READ_HOLDING_REGISTERS
#define MB_FC_WRITE_SINGLE              MB_FC_WRITE_SINGLE_REGISTER
#define MB_FC_WRITE_MULTIPLE            MB_FC_WRITE_MULTIPLE_REGISTERS

#endif // MODBUS_MAP_H