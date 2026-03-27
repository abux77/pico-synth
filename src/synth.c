#include "synth.h"
#include <string.h>

// ─── Global State ─────────────────────────────────────────────────────────────
SynthState g_synth;

// ─── MIDI Note → Frequency ────────────────────────────────────────────────────
// Precomputed: freq = 440.0 * 2^((note-69)/12)
static float midi_note_to_freq(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

// Frequency → 32-bit phase increment
// phase_inc = freq * WAVETABLE_SIZE / SAMPLE_RATE * 2^32
static uint32_t freq_to_phase_inc(float freq) {
    return (uint32_t)((freq * (float)WAVETABLE_SIZE / (float)SAMPLE_RATE) * (float)(1ULL << 32));
}

// ─── Wavetable Generation ─────────────────────────────────────────────────────
static void generate_wavetables(void) {
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float phase = (float)i / (float)WAVETABLE_SIZE; // 0.0 – 1.0

        // --- Sawtooth: ramps from +1 to -1 ---
        // Band-limited using additive synthesis (16 harmonics)
        float saw = 0.0f;
        for (int h = 1; h <= 16; h++) {
            saw += (1.0f / h) * sinf(2.0f * M_PI * h * phase);
        }
        saw *= (2.0f / M_PI); // normalise
        g_synth.wavetable[WAVE_SAWTOOTH][i] = (int16_t)(saw * 32767.0f);

        // --- Square: +1 for first half, -1 for second half ---
        // Band-limited (odd harmonics only, 16 terms)
        float sqr = 0.0f;
        for (int h = 1; h <= 31; h += 2) {
            sqr += (1.0f / h) * sinf(2.0f * M_PI * h * phase);
        }
        sqr *= (4.0f / M_PI);
        if (sqr >  1.0f) sqr =  1.0f;
        if (sqr < -1.0f) sqr = -1.0f;
        g_synth.wavetable[WAVE_SQUARE][i] = (int16_t)(sqr * 32767.0f);

        // --- Triangle: absolute value of sawtooth, phase-shifted ---
        float tri = 0.0f;
        for (int h = 1; h <= 15; h += 2) {
            float sign = ((h / 2) % 2 == 0) ? 1.0f : -1.0f;
            tri += sign * (1.0f / (h * h)) * sinf(2.0f * M_PI * h * phase);
        }
        tri *= (8.0f / (M_PI * M_PI));
        g_synth.wavetable[WAVE_TRIANGLE][i] = (int16_t)(tri * 32767.0f);
    }
}

// ─── Envelope ─────────────────────────────────────────────────────────────────
void envelope_init(Envelope *env) {
    env->attack_time   = 0.005f;   // 5ms
    env->decay_time    = 0.1f;     // 100ms
    env->sustain_level = 0.7f;     // 70%
    env->release_time  = 0.25f;    // 250ms
    env->stage         = ENV_IDLE;
    env->level         = 0.0f;

    float inv_sr = 1.0f / (float)SAMPLE_RATE;
    env->attack_rate  = inv_sr / env->attack_time;
    env->decay_rate   = inv_sr / env->decay_time;
    env->release_rate = inv_sr / env->release_time;
}

void envelope_note_on(Envelope *env) {
    env->stage = ENV_ATTACK;
    // Recalculate rates in case times changed
    float inv_sr = 1.0f / (float)SAMPLE_RATE;
    env->attack_rate  = inv_sr / env->attack_time;
    env->decay_rate   = inv_sr / env->decay_time;
    env->release_rate = inv_sr / env->release_time;
}

void envelope_note_off(Envelope *env) {
    if (env->stage != ENV_IDLE) {
        env->stage = ENV_RELEASE;
        float inv_sr = 1.0f / (float)SAMPLE_RATE;
        env->release_rate = inv_sr / env->release_time;
    }
}

float envelope_process(Envelope *env) {
    switch (env->stage) {
        case ENV_ATTACK:
            env->level += env->attack_rate;
            if (env->level >= 1.0f) {
                env->level = 1.0f;
                env->stage = ENV_DECAY;
            }
            break;

        case ENV_DECAY:
            env->level -= env->decay_rate;
            if (env->level <= env->sustain_level) {
                env->level = env->sustain_level;
                env->stage = ENV_SUSTAIN;
            }
            break;

        case ENV_SUSTAIN:
            env->level = env->sustain_level;
            break;

        case ENV_RELEASE:
            env->level -= env->release_rate;
            if (env->level <= 0.0f) {
                env->level = 0.0f;
                env->stage = ENV_IDLE;
            }
            break;

        case ENV_IDLE:
        default:
            env->level = 0.0f;
            break;
    }
    return env->level;
}

// ─── Biquad Low-Pass Filter ───────────────────────────────────────────────────
// Bilinear transform, Butterworth / parametric Q
void filter_update_coeffs(BiquadFilter *f, float cutoff_hz, float Q) {
    // Clamp cutoff to safe range
    if (cutoff_hz < 20.0f)       cutoff_hz = 20.0f;
    if (cutoff_hz > 20000.0f)    cutoff_hz = 20000.0f;
    if (Q < 0.5f)                Q = 0.5f;
    if (Q > 10.0f)               Q = 10.0f;

    float w0    = 2.0f * M_PI * cutoff_hz / (float)SAMPLE_RATE;
    float cos_w = cosf(w0);
    float sin_w = sinf(w0);
    float alpha = sin_w / (2.0f * Q);

    float a0 =  1.0f + alpha;
    f->b0    = ((1.0f - cos_w) / 2.0f) / a0;
    f->b1    =  (1.0f - cos_w)         / a0;
    f->b2    = f->b0;
    f->a1    = (-2.0f * cos_w)         / a0;
    f->a2    = ( 1.0f - alpha)         / a0;
}

float filter_process(BiquadFilter *f, float x) {
    // Transposed Direct Form II — numerically stable
    float y  = f->b0 * x + f->z1;
    f->z1    = f->b1 * x - f->a1 * y + f->z2;
    f->z2    = f->b2 * x - f->a2 * y;
    return y;
}

// ─── Synth Init ───────────────────────────────────────────────────────────────
void synth_init(void) {
    memset(&g_synth, 0, sizeof(SynthState));

    g_synth.waveform         = WAVE_SAWTOOTH;
    g_synth.filter_cutoff    = 4000.0f;
    g_synth.filter_resonance = 0.707f;  // Butterworth (no resonance peak)

    generate_wavetables();
    envelope_init(&g_synth.voice.env);
    filter_update_coeffs(&g_synth.voice.filter,
                         g_synth.filter_cutoff,
                         g_synth.filter_resonance);

    g_synth.voice.waveform = g_synth.waveform;
    g_synth.voice.active   = false;
}

// ─── Voice Management ─────────────────────────────────────────────────────────
void synth_note_on(uint8_t note, uint8_t velocity) {
    Voice *v      = &g_synth.voice;
    v->active     = true;
    v->note       = note;
    v->velocity   = velocity;
    v->waveform   = g_synth.waveform;
    v->phase      = 0;  // reset phase on new note (monophonic)
    v->phase_inc  = freq_to_phase_inc(midi_note_to_freq(note));

    filter_update_coeffs(&v->filter,
                         g_synth.filter_cutoff,
                         g_synth.filter_resonance);

    envelope_note_on(&v->env);
}

void synth_note_off(uint8_t note) {
    Voice *v = &g_synth.voice;
    // Only release if this note is the active one
    if (v->active && v->note == note) {
        envelope_note_off(&v->env);
    }
}

void synth_set_waveform(WaveformType w) {
    if (w < WAVE_COUNT) {
        g_synth.waveform = w;
        g_synth.voice.waveform = w;
    }
}

void synth_set_filter(float cutoff_hz, float resonance) {
    g_synth.filter_cutoff    = cutoff_hz;
    g_synth.filter_resonance = resonance;
    filter_update_coeffs(&g_synth.voice.filter, cutoff_hz, resonance);
}

// ─── Sample Generation (called per-sample from audio ISR) ────────────────────
int16_t synth_next_sample(void) {
    Voice *v = &g_synth.voice;

    float env_level = envelope_process(&v->env);

    // Kill voice when envelope fully released
    if (v->env.stage == ENV_IDLE && env_level == 0.0f) {
        v->active = false;
    }

    if (!v->active) return 0;

    // --- Wavetable lookup with linear interpolation ---
    // Upper 11 bits = integer table index, lower 21 bits = fractional part
    uint32_t idx_int  = v->phase >> (32 - 11);          // 0 – 2047
    uint32_t idx_frac = (v->phase >> (32 - 11 - 21)) & 0x1FFFFF;  // 21-bit fraction
    uint32_t idx_next = (idx_int + 1) & (WAVETABLE_SIZE - 1);

    int16_t s0 = g_synth.wavetable[v->waveform][idx_int];
    int16_t s1 = g_synth.wavetable[v->waveform][idx_next];

    // Linear interpolation: s0 + frac * (s1 - s0)
    float frac   = (float)idx_frac / (float)(1 << 21);
    float sample = (float)s0 + frac * (float)(s1 - s0);

    // Advance phase
    v->phase += v->phase_inc;

    // Apply envelope
    sample *= env_level;

    // Apply velocity (scale 0–127 → 0.0–1.0)
    sample *= (float)v->velocity / 127.0f;

    // Apply low-pass filter
    sample = filter_process(&v->filter, sample);

    // Clamp and return
    if (sample >  32767.0f) sample =  32767.0f;
    if (sample < -32767.0f) sample = -32767.0f;

    return (int16_t)sample;
}
