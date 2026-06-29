// adc_dma.h
// ADC driver for ESP32 - single-shot mode (reliable)

#ifndef ADC_DMA_H
#define ADC_DMA_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Function Prototypes
// ============================================================

// Initialize ADC for 8 channels
int wqms_adc_dma_init(void);

// Start ADC (single-shot mode - ready to read)
int wqms_adc_dma_start(void);

// Stop ADC (no-op in single-shot mode)
int wqms_adc_dma_stop(void);

// Get the latest ADC reading for a channel (averaged)
uint16_t wqms_adc_dma_get_raw(int channel);

// Get the latest voltage for a channel (mV)
int wqms_adc_dma_get_voltage_mv(int channel);

// Get the latest value as float (0-5V)
float wqms_adc_dma_get_voltage(int channel);

// Check if new data is available (always true in single-shot mode)
int wqms_adc_dma_data_ready(void);

// Reset the data-ready flag (no-op in single-shot mode)
void wqms_adc_dma_clear_ready(void);

#endif // ADC_DMA_H