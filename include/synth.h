#pragma once

#include "pico/stdlib.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// ─── Audio Configuration ──────────────────────────────────────────────────────
#define SAMPLE_RATE         44100
#define AUDIO_BUFFER_SIZE   256         // samples per DMA buffer (stereo = x2 words)
#define WAVETABLE_SIZE      2048        // must be power of 2
#define PHASE_ACCUMULATOR_BITS 32       // 32-bit phase accumulator

// ─── Hardware Pins ────────────────────────────────────────────────────────────
// I2S DAC (PCM5102A)
#define I2S_DATA_PIN        26          // GP26 → DIN on PCM5102A
#define I2S_CLOCK_PIN       27          // GP27 → BCK (PIO handles LRCK = BCK+1)

// UART MIDI
#define MIDI_UART_ID        uart1
#define MIDI_UART_TX_PIN    4           // GP4  (unused but required by SDK)
#define MIDI_UART_RX_PIN    5           // GP5  → MIDI optocoupler output
#define MIDI_BAUD           31250

// ADC - Knobs
#define ADC_CUTOFF_PIN      28          // GP28 / ADC2 → cutoff pot
#define ADC_RESONANCE_PIN   27          // GP27 / ADC1 → resonance pot
// Note: don't conflict with I2S pins in your actual wiring; adjust as needed.
// Safer alternative: use GP26(ADC0) and GP27(ADC1) only if I2S uses different pins.
// Recommended physical layout: I2S on GP0/GP1/GP2, knobs on GP26/GP27.

// ─── MIDI ─────────────────────────────────────────────────────────────────────
#define MIDI_CHANNEL        0           // 0 = channel 1, 0xFF = omni
#define CC_WAVEFORM         74          // CC74 selects waveform
#define CC_CUTOFF           71          // CC71 controls filter cutoff
#define CC_RESONANCE        72          // CC72 controls resonance

// ─── Waveform Types ───────────────────────────────────────────────────────────
typedef enum {
    WAVE_SAWTOOTH = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_COUNT    = 3
} WaveformType;

// ─── ADSR Envelope ────────────────────────────────────────────────────────────
typedef enum {
    ENV_IDLE    = 0,
    ENV_ATTACK  = 1,
    ENV_DECAY   = 2,
    ENV_SUSTAIN = 3,
    ENV_RELEASE = 4
} EnvStage;

typedef struct {
    float attack_time;      // seconds
    float decay_time;       // seconds
    float sustain_level;    // 0.0 - 1.0
    float release_time;     // seconds

    EnvStage stage;
    float    level;         // current output 0.0 - 1.0
    float    attack_rate;
    float    decay_rate;
    float    release_rate;
} Envelope;

// ─── Biquad Low-Pass Filter (Transposed Direct Form II) ───────────────────────
typedef struct {
    float b0, b1, b2;       // feedforward coefficients
    float a1, a2;           // feedback coefficients (a0 normalised to 1)
    float z1, z2;           // delay line state
} BiquadFilter;

// ─── Voice ────────────────────────────────────────────────────────────────────
typedef struct {
    bool     active;
    uint8_t  note;          // MIDI note number
    uint8_t  velocity;      // MIDI velocity

    uint32_t phase;         // 32-bit phase accumulator
    uint32_t phase_inc;     // phase increment per sample

    WaveformType waveform;

    Envelope env;
    BiquadFilter filter;
} Voice;

// ─── Global Synth State ───────────────────────────────────────────────────────
typedef struct {
    Voice        voice;

    WaveformType waveform;
    float        filter_cutoff;     // Hz, 20 – 20000
    float        filter_resonance;  // 0.5 – 10.0 (Q)

    // Wavetables (generated at boot)
    int16_t      wavetable[WAVE_COUNT][WAVETABLE_SIZE];
} SynthState;

extern SynthState g_synth;

// ─── Function Prototypes ──────────────────────────────────────────────────────
void synth_init(void);
void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);
void synth_set_waveform(WaveformType w);
void synth_set_filter(float cutoff_hz, float resonance);
int16_t synth_next_sample(void);

void envelope_init(Envelope *env);
void envelope_note_on(Envelope *env);
void envelope_note_off(Envelope *env);
float envelope_process(Envelope *env);

void filter_update_coeffs(BiquadFilter *f, float cutoff_hz, float Q);
float filter_process(BiquadFilter *f, float x);
