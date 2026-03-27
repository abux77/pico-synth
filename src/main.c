#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"

#include "synth.h"
#include "i2s_audio.h"
#include "midi.h"
#include "knobs.h"

// ─── Pin Assignments (adjust to match your wiring) ───────────────────────────
//
//  PCM5102A DAC:
//    GP0 → DIN  (I2S data)
//    GP1 → BCK  (I2S bit clock)    PIO also drives GP2 as LRCK
//    GP2 → LRCK (auto-driven by PIO side-set)
//    GND → SCK, FMT, DEMP
//    3V3 → XSMT (unmute)
//
//  UART MIDI (optocoupler output → GP5):
//    GP5 → UART1 RX
//
//  Pots (10kΩ log recommended for cutoff, linear for resonance):
//    GP26 (ADC0) → cutoff wiper
//    GP27 (ADC1) → resonance wiper

#define I2S_DATA_GPIO   0
#define I2S_CLOCK_GPIO  1   // LRCK will be on GPIO 2 (auto side-set)

// ─── Core 1: MIDI + Knobs ────────────────────────────────────────────────────
// Core 0 handles audio DMA IRQ (latency-critical).
// Core 1 handles MIDI parsing and ADC reads (latency-tolerant).

static void core1_entry(void) {
    midi_uart_init();
    knobs_init();

    // Small startup delay to let USB enumerate
    sleep_ms(500);

    while (true) {
        // TinyUSB device task (must be called regularly)
        tud_task();

        // Poll MIDI sources
        midi_uart_poll();
        midi_usb_poll();

        // Poll ADC knobs every ~5ms
        static uint32_t last_knob_ms = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_knob_ms >= 5) {
            knobs_poll();
            last_knob_ms = now;
        }
    }
}

// ─── Core 0: Audio + USB Init ────────────────────────────────────────────────
int main(void) {
    // Run at 132 MHz for headroom with audio + USB
    // (set_sys_clock_khz is a SDK helper; needs hardware_clocks)
    // set_sys_clock_khz(132000, true);

    stdio_init_all();

    // Initialise TinyUSB (must happen on Core 0 before Core 1 starts)
    tusb_init();

    // Initialise synth engine
    synth_init();

    // Initialise I2S + DMA audio (starts playing immediately via DMA IRQ)
    i2s_init(I2S_DATA_GPIO, I2S_CLOCK_GPIO);

    // Launch MIDI + knob handling on Core 1
    multicore_launch_core1(core1_entry);

    // Core 0 main loop — audio is driven entirely by DMA IRQ,
    // so Core 0 can sleep. Uncomment stdio_printf calls here for debugging.
    while (true) {
        tight_loop_contents(); // NOP loop; DMA IRQ does all the work
    }

    return 0;
}
