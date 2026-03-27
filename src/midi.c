#include "midi.h"
#include "synth.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "tusb.h"

// ─── UART MIDI Init ───────────────────────────────────────────────────────────
void midi_uart_init(void) {
    uart_init(MIDI_UART_ID, MIDI_BAUD);
    gpio_set_function(MIDI_UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MIDI_UART_TX_PIN, GPIO_FUNC_UART);

    // MIDI: 8 data bits, no parity, 1 stop bit
    uart_set_format(MIDI_UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART_ID, true);
}

// ─── MIDI Running Status Parser ───────────────────────────────────────────────
typedef struct {
    uint8_t status;     // current status byte (with running status)
    uint8_t data[2];    // data byte buffer
    uint8_t data_idx;   // how many data bytes collected so far
    uint8_t data_len;   // how many data bytes this message needs
} MidiParser;

static MidiParser parser = {0};

// Returns the number of data bytes expected for a given status byte
static uint8_t midi_data_length(uint8_t status) {
    uint8_t type = status & 0xF0;
    switch (type) {
        case 0x80: return 2; // Note Off
        case 0x90: return 2; // Note On
        case 0xA0: return 2; // Aftertouch
        case 0xB0: return 2; // CC
        case 0xC0: return 1; // Program Change
        case 0xD0: return 1; // Channel Pressure
        case 0xE0: return 2; // Pitch Bend
        default:   return 0; // System messages (ignore for now)
    }
}

static void midi_dispatch(uint8_t status, uint8_t d0, uint8_t d1) {
    uint8_t type    = status & 0xF0;
    uint8_t channel = status & 0x0F;

    // Filter by channel (MIDI_CHANNEL 0xFF = omni)
    if (MIDI_CHANNEL != 0xFF && channel != MIDI_CHANNEL) return;

    switch (type) {
        case 0x90:  // Note On
            if (d1 == 0) {
                // Velocity 0 = Note Off (MIDI spec)
                synth_note_off(d0);
            } else {
                synth_note_on(d0, d1);
            }
            break;

        case 0x80:  // Note Off
            synth_note_off(d0);
            break;

        case 0xB0:  // Control Change
            switch (d0) {
                case CC_WAVEFORM:
                    // CC74: 0-42=Saw, 43-84=Square, 85-127=Triangle
                    if      (d1 < 43)  synth_set_waveform(WAVE_SAWTOOTH);
                    else if (d1 < 85)  synth_set_waveform(WAVE_SQUARE);
                    else               synth_set_waveform(WAVE_TRIANGLE);
                    break;

                case CC_CUTOFF: {
                    // CC71: 0-127 → 80Hz – 18000Hz (exponential)
                    float norm    = d1 / 127.0f;
                    float cutoff  = 80.0f * powf(18000.0f / 80.0f, norm);
                    synth_set_filter(cutoff, g_synth.filter_resonance);
                    break;
                }

                case CC_RESONANCE: {
                    // CC72: 0-127 → Q 0.5 – 10.0
                    float Q = 0.5f + (d1 / 127.0f) * 9.5f;
                    synth_set_filter(g_synth.filter_cutoff, Q);
                    break;
                }

                case 0x7B: // All Notes Off
                    synth_note_off(g_synth.voice.note);
                    break;
            }
            break;

        // Pitch bend, aftertouch etc. can be added here
        default:
            break;
    }
}

void midi_parse_byte(uint8_t byte) {
    if (byte & 0x80) {
        // Status byte
        if (byte == 0xF8 || byte == 0xFE) return; // ignore clock/active sense
        if (byte >= 0xF0) return;                  // ignore sysex/system for now

        parser.status   = byte;
        parser.data_idx = 0;
        parser.data_len = midi_data_length(byte);
    } else {
        // Data byte
        if (parser.status == 0) return; // ignore until we have a status
        if (parser.data_idx < 2) {
            parser.data[parser.data_idx++] = byte;
        }
        if (parser.data_idx >= parser.data_len) {
            midi_dispatch(parser.status, parser.data[0], parser.data[1]);
            parser.data_idx = 0; // ready for running status
        }
    }
}

// ─── UART Poll ────────────────────────────────────────────────────────────────
void midi_uart_poll(void) {
    while (uart_is_readable(MIDI_UART_ID)) {
        uint8_t byte = uart_getc(MIDI_UART_ID);
        midi_parse_byte(byte);
    }
}

// ─── USB MIDI Poll ────────────────────────────────────────────────────────────
// The Pico acts as a USB MIDI device (class-compliant).
// A DAW or controller connects to it over USB.
void midi_usb_poll(void) {
    if (!tud_midi_available()) return;

    uint8_t packet[4]; // USB MIDI packets are 4 bytes (cable number + 3 MIDI)
    while (tud_midi_packet_read(packet)) {
        // Bytes 1-3 are the MIDI message (byte 0 is USB cable/code index)
        uint8_t code = packet[0] & 0x0F;
        // Skip sysex and single-byte system messages for simplicity
        if (code >= 0x08) {
            midi_parse_byte(packet[1]);
            if (code != 0x0C && code != 0x0D) { // not prog-change/chan-pressure
                midi_parse_byte(packet[2]);
            }
            if (code == 0x08 || code == 0x09 || code == 0x0A ||
                code == 0x0B || code == 0x0E) {
                midi_parse_byte(packet[3]);
            }
        }
    }
}
