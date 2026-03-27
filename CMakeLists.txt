cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (set PICO_SDK_PATH in environment or pass via -D)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_synth C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ─── Source Files ─────────────────────────────────────────────────────────────
add_executable(pico_synth
    src/main.c
    src/synth.c
    src/i2s_audio.c
    src/midi.c
    src/knobs.c
    src/usb_descriptors.c
)

# ─── Include Paths ────────────────────────────────────────────────────────────
target_include_directories(pico_synth PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src       # for tusb_config.h
)

# ─── SDK Libraries ────────────────────────────────────────────────────────────
target_link_libraries(pico_synth
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_clocks
    hardware_uart
    tinyusb_device        # USB MIDI device stack
    tinyusb_board
    m                     # libm (for sinf, powf, etc.)
)

# ─── USB / stdio ──────────────────────────────────────────────────────────────
# Disable stdio over USB so TinyUSB owns the USB peripheral
pico_enable_stdio_usb(pico_synth 0)
pico_enable_stdio_uart(pico_synth 1)  # debug via GP0/GP1 if needed

# ─── Generate UF2 / BIN ───────────────────────────────────────────────────────
pico_add_extra_outputs(pico_synth)
