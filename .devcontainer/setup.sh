#!/bin/bash
set -e

echo "==> Installing ARM toolchain and build tools..."
sudo apt-get update -q
sudo apt-get install -y -q \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3 \
    ninja-build \
    pkg-config

echo "==> Cloning Raspberry Pi Pico SDK..."
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk
git submodule update --init --depth 1   # pulls in TinyUSB

echo "==> Setting PICO_SDK_PATH globally..."
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.bashrc
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.profile
export PICO_SDK_PATH=~/pico-sdk

echo "==> Creating build directory..."
cd /workspaces/pico-synth       # Codespaces mounts your repo here

echo "==> Creating missing header files if needed..."
[ ! -f include/i2s_audio.h ] && cat > include/i2s_audio.h << 'HEADER'
#pragma once
#include "pico/stdlib.h"
#include "synth.h"
#include <stdint.h>

#define I2S_PIO             pio0
#define I2S_SM              0

void i2s_init(uint data_pin, uint clock_pin_base);
void i2s_dma_irq_handler(void);
HEADER

[ ! -f include/midi.h ] && cat > include/midi.h << 'HEADER'
#pragma once
#include "pico/stdlib.h"
#include <stdint.h>

void midi_uart_init(void);
void midi_uart_poll(void);
void midi_usb_poll(void);
void midi_parse_byte(uint8_t byte);
HEADER

[ ! -f include/knobs.h ] && cat > include/knobs.h << 'HEADER'
#pragma once
#include "pico/stdlib.h"

void knobs_init(void);
void knobs_poll(void);
HEADER

[ ! -f include/tusb_config.h ] && cat > include/tusb_config.h << 'HEADER'
#pragma once

#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUSB_OS             OPT_OS_PICO

#define CFG_TUD_MIDI            1
#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

#define CFG_TUSB_DEBUG          0
HEADER

mkdir -p build
cd build
cmake .. -G Ninja

echo ""
echo "✅ Setup complete!"
echo ""
echo "To build your firmware, run:"
echo "  cd build && ninja"
echo ""
echo "The output file will be at:"
echo "  build/pico_synth.uf2"