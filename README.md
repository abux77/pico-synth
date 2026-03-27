# Pico Synth

A monophonic wavetable synthesizer for the Raspberry Pi Pico.

**Features:**
- Sawtooth, Square, Triangle waveforms (band-limited)
- ADSR amplitude envelope
- Biquad low-pass filter (cutoff + resonance knobs)
- USB MIDI device (class-compliant, works with any DAW)
- UART MIDI (standard 5-pin DIN via optocoupler)
- MIDI CC control for waveform, cutoff, and resonance
- I2S audio output via PCM5102A (44.1kHz, 16-bit)
- Dual-core: audio DMA on Core 0, MIDI + ADC on Core 1

---

## Wiring

```
Raspberry Pi Pico          PCM5102A DAC Board
─────────────────          ─────────────────────
GP0  ──────────────────►  DIN   (I2S data)
GP1  ──────────────────►  BCK   (I2S bit clock)
GP2  ──────────────────►  LRCK  (I2S word clock) ← driven by PIO side-set
3V3  ──────────────────►  VCC
GND  ──────────────────►  GND
GND  ──────────────────►  SCK   (tie low)
GND  ──────────────────►  FMT   (I2S format)
GND  ──────────────────►  DEMP  (no de-emphasis)
3V3  ──────────────────►  XSMT  (unmute)


Raspberry Pi Pico          UART MIDI (optocoupler)
─────────────────          ──────────────────────
GP5  ◄─────────────────   OUT   (6N138 or PC-900V output)
GND  ──────────────────►  GND


Raspberry Pi Pico          Potentiometers (10kΩ)
─────────────────          ─────────────────────
3V3  ──────────────────►  Top end of both pots
GND  ──────────────────►  Bottom end of both pots
GP26 (ADC0) ◄──────────  Cutoff wiper
GP27 (ADC1) ◄──────────  Resonance wiper
```

### UART MIDI Optocoupler Circuit
```
MIDI DIN Pin 4 ──┬── 220Ω ──► LED anode  (6N138 pin 2)
MIDI DIN Pin 5 ──┘           LED cathode (6N138 pin 3) → GND
                             Collector   (6N138 pin 6) → 3V3 via 4.7kΩ
                             Emitter     (6N138 pin 5) → GND
                             Collector output ──────────► GP5
```

---

## Build Instructions

### Prerequisites
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) installed
- CMake 3.13+
- arm-none-eabi-gcc toolchain

### Build
```bash
export PICO_SDK_PATH=/path/to/pico-sdk

mkdir build && cd build
cmake ..
make -j4
```

This produces `pico_synth.uf2` in the build directory.

### Flash
1. Hold BOOTSEL on the Pico and plug into USB
2. Copy `pico_synth.uf2` to the RPI-RP2 drive that appears
3. Pico reboots and starts running

---

## Usage

### Waveform Selection
Turn a knob or send **CC74** on MIDI channel 1:
- Value 0–42   → Sawtooth
- Value 43–84  → Square
- Value 85–127 → Triangle

### Filter Control
| Control       | Hardware      | MIDI CC |
|---------------|---------------|---------|
| Cutoff        | GP26 pot      | CC71    |
| Resonance     | GP27 pot      | CC72    |

Hardware knobs and MIDI CC both work simultaneously. MIDI CC takes priority if sent — turning a knob will re-assert hardware control.

### USB MIDI
The Pico enumerates as **"Pico Synth MIDI"** on USB. Select it as a MIDI output in your DAW. All messages are received on Channel 1 (omni can be enabled in `synth.h`).

### UART MIDI
Connect a standard MIDI controller or sequencer to the 5-pin DIN input via an optocoupler circuit. Responds on Channel 1.

---

## Architecture

```
Core 0                          Core 1
──────────────────────────      ──────────────────────────
main()                          core1_entry()
  tusb_init()                     midi_uart_init()
  synth_init()                    knobs_init()
  i2s_init()                      loop:
  multicore_launch_core1()          tud_task()
  tight_loop_contents()             midi_uart_poll()
                                    midi_usb_poll()
DMA IRQ (every ~5.8ms):             knobs_poll() [every 5ms]
  fill_audio_buffer()
    synth_next_sample() × 256
      envelope_process()
      wavetable lookup + lerp
      filter_process()
```

### Key Parameters (synth.h)
| Parameter        | Default     | Description                    |
|-----------------|-------------|--------------------------------|
| SAMPLE_RATE     | 44100       | Audio sample rate              |
| AUDIO_BUFFER_SIZE | 256       | DMA buffer size (samples)      |
| WAVETABLE_SIZE  | 2048        | Wavetable resolution           |
| MIDI_CHANNEL    | 0 (ch. 1)   | 0xFF for omni                  |

### Envelope Defaults (synth.c)
| Stage    | Time   |
|----------|--------|
| Attack   | 5ms    |
| Decay    | 100ms  |
| Sustain  | 70%    |
| Release  | 250ms  |

These can be made MIDI-controllable by adding CC handlers in `midi.c`.

---

## Extending the Synth

**Add envelope CC control:** Map CC73 (attack), CC75 (decay), etc. in `midi_dispatch()` in `midi.c`.

**Add pitch bend:** Handle `0xE0` in `midi_dispatch()` and multiply `phase_inc` by a bend factor in `synth_next_sample()`.

**Add a second filter pole:** Chain two `BiquadFilter` stages in the voice for a steeper 24dB/oct slope.

**Add portamento/glide:** Instead of snapping `phase_inc` on note-on, interpolate it toward the target frequency over N samples.
