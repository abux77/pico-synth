#pragma once
#include "pico/stdlib.h"
#include "synth.h"
#include <stdint.h>

#define I2S_PIO             pio0
#define I2S_SM              0

void i2s_init(uint data_pin, uint clock_pin_base);
void i2s_dma_irq_handler(void);
