#pragma once
#include "pico/stdlib.h"
#include <stdint.h>

void midi_uart_init(void);
void midi_uart_poll(void);
void midi_usb_poll(void);
void midi_parse_byte(uint8_t byte);
