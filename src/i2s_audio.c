#include "i2s_audio.h"
#include "synth.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

// ─── PIO I2S Program ──────────────────────────────────────────────────────────
// This minimal PIO program clocks out 32 bits per channel (64 bits per frame).
// It expects 32-bit words pushed into the TX FIFO by DMA.
// BCK  = side-set pin 0 (clock_pin_base + 0)
// LRCK = side-set pin 1 (clock_pin_base + 1)
// DATA = out pin        (data_pin)

// Assembled PIO program — generated from the following PIO assembly:
//
// .program i2s_out
// .side_set 2
// .wrap_target
// public entry_point:
//   set x, 30          side 0b01   ; LRCK=0 (left), BCK=1, preload bit counter
//   pull noblock       side 0b00   ; fetch left word from FIFO
// bitloop_left:
//   out pins, 1        side 0b00   ; shift out 1 bit, BCK=0
//   jmp x-- bitloop_left side 0b01 ; BCK=1, count down
//   out pins, 1        side 0b10   ; last left bit out, LRCK→1 (right), BCK=0
//   set x, 30          side 0b11   ; BCK=1
//   pull noblock       side 0b10   ; fetch right word
// bitloop_right:
//   out pins, 1        side 0b10   ; BCK=0
//   jmp x-- bitloop_right side 0b11 ; BCK=1
//   out pins, 1        side 0b00   ; last right bit, LRCK→0 (left), BCK=0
// .wrap

static const uint16_t i2s_program_instructions[] = {
    //     instr / side / delay
    0x7821, //  0: set    x, 30              side 0b01
    0x5400, //  1: pull   noblock            side 0b00
    0x5001, //  2: out    pins, 1            side 0b00  [0]
    0x1842, //  3: jmp    x--, 2             side 0b01
    0x6001, //  4: out    pins, 1            side 0b10  [0]
    0x7e21, //  5: set    x, 30              side 0b11
    0x5600, //  6: pull   noblock            side 0b10
    0x5801, //  7: out    pins, 1            side 0b10  [0]
    0x1ec7, //  8: jmp    x--, 7             side 0b11
    0x4001, //  9: out    pins, 1            side 0b00  [0]
};

static const struct pio_program i2s_program = {
    .instructions = i2s_program_instructions,
    .length       = 10,
    .origin       = -1,
};

// ─── DMA Double Buffers ───────────────────────────────────────────────────────
static uint32_t audio_buf[2][AUDIO_BUFFER_SIZE * 2]; // stereo: L+R per frame
static int      buf_idx  = 0;
static int      dma_ch_a, dma_ch_b;

// ─── Fill Buffer with Synth Samples ──────────────────────────────────────────
static void fill_audio_buffer(uint32_t *buf, int num_frames) {
    for (int i = 0; i < num_frames; i++) {
        int16_t sample = synth_next_sample();
        // PCM5102A expects 32-bit left-justified: MSB first, shift left 16
        uint32_t word = (uint32_t)(uint16_t)sample << 16;
        buf[i * 2 + 0] = word; // Left
        buf[i * 2 + 1] = word; // Right (mono: duplicate)
    }
}

// ─── DMA IRQ Handler ─────────────────────────────────────────────────────────
void i2s_dma_irq_handler(void) {
    // Figure out which channel just finished and refill it
    if (dma_channel_get_irq0_status(dma_ch_a)) {
        dma_channel_acknowledge_irq0(dma_ch_a);
        fill_audio_buffer(audio_buf[0], AUDIO_BUFFER_SIZE);
        dma_channel_set_read_addr(dma_ch_a, audio_buf[0], false);
    }
    if (dma_channel_get_irq0_status(dma_ch_b)) {
        dma_channel_acknowledge_irq0(dma_ch_b);
        fill_audio_buffer(audio_buf[1], AUDIO_BUFFER_SIZE);
        dma_channel_set_read_addr(dma_ch_b, audio_buf[1], false);
    }
}

// ─── I2S Init ─────────────────────────────────────────────────────────────────
void i2s_init(uint data_pin, uint clock_pin_base) {
    // --- Load PIO program ---
    uint offset = pio_add_program(I2S_PIO, &i2s_program);
    pio_sm_config cfg = pio_get_default_sm_config();

    // Out pin = data_pin
    sm_config_set_out_pins(&cfg, data_pin, 1);
    // Side-set = clock_pin_base (BCK) and clock_pin_base+1 (LRCK)
    sm_config_set_sideset_pins(&cfg, clock_pin_base);
    sm_config_set_sideset(&cfg, 2, false, false);

    // 32-bit words, shift left (MSB first), auto-pull threshold = 32
    sm_config_set_out_shift(&cfg, false, true, 32);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

    // Clock: system_clock / (2 * (divider))
    // We need BCK = SAMPLE_RATE * 64  (32 bits × 2 channels)
    // BCK toggles every PIO cycle → PIO runs at BCK rate
    // PIO clock = 2 × BCK = 2 × SAMPLE_RATE × 64
    float pio_freq = 2.0f * SAMPLE_RATE * 64.0f;
    float div      = (float)clock_get_hz(clk_sys) / pio_freq;
    sm_config_set_clkdiv(&cfg, div);

    // Initialise GPIO
    pio_gpio_init(I2S_PIO, data_pin);
    pio_gpio_init(I2S_PIO, clock_pin_base);
    pio_gpio_init(I2S_PIO, clock_pin_base + 1);
    pio_sm_set_consecutive_pindirs(I2S_PIO, I2S_SM, data_pin,      1, true);
    pio_sm_set_consecutive_pindirs(I2S_PIO, I2S_SM, clock_pin_base, 2, true);

    pio_sm_init(I2S_PIO, I2S_SM, offset, &cfg);
    pio_sm_set_enabled(I2S_PIO, I2S_SM, true);

    // --- Pre-fill buffers ---
    fill_audio_buffer(audio_buf[0], AUDIO_BUFFER_SIZE);
    fill_audio_buffer(audio_buf[1], AUDIO_BUFFER_SIZE);

    // --- Configure DMA channel A ---
    dma_ch_a = dma_claim_unused_channel(true);
    dma_channel_config dma_cfg_a = dma_channel_get_default_config(dma_ch_a);
    channel_config_set_transfer_data_size(&dma_cfg_a, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg_a, true);
    channel_config_set_write_increment(&dma_cfg_a, false);
    channel_config_set_dreq(&dma_cfg_a, pio_get_dreq(I2S_PIO, I2S_SM, true));
    // Chain to channel B when done
    channel_config_set_chain_to(&dma_cfg_a, dma_ch_b = dma_claim_unused_channel(true));

    dma_channel_configure(dma_ch_a, &dma_cfg_a,
        &I2S_PIO->txf[I2S_SM],         // write to PIO TX FIFO
        audio_buf[0],                   // read from buffer 0
        AUDIO_BUFFER_SIZE * 2,          // stereo words
        false);                         // don't start yet

    // --- Configure DMA channel B ---
    dma_channel_config dma_cfg_b = dma_channel_get_default_config(dma_ch_b);
    channel_config_set_transfer_data_size(&dma_cfg_b, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg_b, true);
    channel_config_set_write_increment(&dma_cfg_b, false);
    channel_config_set_dreq(&dma_cfg_b, pio_get_dreq(I2S_PIO, I2S_SM, true));
    channel_config_set_chain_to(&dma_cfg_b, dma_ch_a); // chain back to A (ping-pong)

    dma_channel_configure(dma_ch_b, &dma_cfg_b,
        &I2S_PIO->txf[I2S_SM],
        audio_buf[1],
        AUDIO_BUFFER_SIZE * 2,
        false);

    // --- Enable IRQ on both channels ---
    dma_channel_set_irq0_enabled(dma_ch_a, true);
    dma_channel_set_irq0_enabled(dma_ch_b, true);
    irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // --- Start channel A (B will auto-chain) ---
    dma_channel_start(dma_ch_a);
}
