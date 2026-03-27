#include "knobs.h"
#include "synth.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <math.h>

// ADC channel mappings (GP26=ADC0, GP27=ADC1, GP28=ADC2)
#define ADC_CH_CUTOFF     0   // GP26 → ADC0
#define ADC_CH_RESONANCE  1   // GP27 → ADC1

// Smoothing factor: 0.0 = no smoothing, close to 1.0 = very smooth/slow
#define ADC_SMOOTH        0.05f

// Deadband: don't update filter if change is below this (avoids noise jitter)
#define ADC_DEADBAND      8   // out of 4096

static float smooth_cutoff    = 0.0f;
static float smooth_resonance = 0.0f;
static uint16_t last_cutoff_raw    = 0xFFFF;
static uint16_t last_resonance_raw = 0xFFFF;

void knobs_init(void) {
    adc_init();
    adc_gpio_init(26); // GP26 = ADC0 = cutoff
    adc_gpio_init(27); // GP27 = ADC1 = resonance

    // Initial read to seed smoothing values
    adc_select_input(ADC_CH_CUTOFF);
    smooth_cutoff = (float)adc_read();
    adc_select_input(ADC_CH_RESONANCE);
    smooth_resonance = (float)adc_read();
}

void knobs_poll(void) {
    // ── Read cutoff knob ──────────────────────────────────────────────────────
    adc_select_input(ADC_CH_CUTOFF);
    uint16_t raw_cutoff = adc_read();

    // Exponential moving average smoothing
    smooth_cutoff = smooth_cutoff + ADC_SMOOTH * ((float)raw_cutoff - smooth_cutoff);
    uint16_t smooth_int = (uint16_t)smooth_cutoff;

    bool cutoff_changed = (smooth_int > last_cutoff_raw + ADC_DEADBAND) ||
                          (smooth_int + ADC_DEADBAND < last_cutoff_raw);

    // ── Read resonance knob ───────────────────────────────────────────────────
    adc_select_input(ADC_CH_RESONANCE);
    uint16_t raw_resonance = adc_read();

    smooth_resonance = smooth_resonance + ADC_SMOOTH * ((float)raw_resonance - smooth_resonance);
    uint16_t smooth_res_int = (uint16_t)smooth_resonance;

    bool resonance_changed = (smooth_res_int > last_resonance_raw + ADC_DEADBAND) ||
                              (smooth_res_int + ADC_DEADBAND < last_resonance_raw);

    // ── Update synth filter if anything changed ────────────────────────────────
    if (cutoff_changed || resonance_changed) {
        // Cutoff: 0-4095 → 80Hz – 18000Hz exponential mapping
        float norm_cut  = smooth_cutoff / 4095.0f;
        float cutoff_hz = 80.0f * powf(18000.0f / 80.0f, norm_cut);

        // Resonance: 0-4095 → Q 0.5 – 10.0 linear
        float Q = 0.5f + (smooth_resonance / 4095.0f) * 9.5f;

        synth_set_filter(cutoff_hz, Q);

        last_cutoff_raw    = smooth_int;
        last_resonance_raw = smooth_res_int;
    }
}
