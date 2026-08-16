# Architecture

## The rule

No module ever names the hardware of another module.

```
             MIDI INPUTS
                  │
                  ▼
            MIDI ROUTER
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
 SOUND ENGINE         VALVE ENGINE
        │                   │
        ▼                   ▼
 AUDIO ENGINE        ACTUATOR HAL
        │                   │
        ▼                   ▼
 AUDIO BACKEND        SERVO / SOLENOID
        │
        ▼
 AMPLIFIER PROFILE
        │
        ▼
 SPEAKER PROFILE
        │
        ▼
ACOUSTIC COUPLING
        │
        ▼
     TRUMPET
```

Concretely this means:

* replacing `PCM5102A` by `MAX98357A`, `ES8388`, `WM8960` or `TAS5760M`
  changes nothing in the MIDI router, the sound engine, the valve controller
  or the web UI architecture;
* replacing a servo by a solenoid changes nothing in the MIDI engine;
* replacing BLE MIDI by USB, DIN, RTP-MIDI or the WebSocket bridge changes
  nothing in the sound engine.

The interfaces that carry that promise:

| Interface | File | Implementations |
|---|---|---|
| `IMidiTransport` | `src/midi/IMidiTransport.h` | USB, BLE, DIN, RTP, WebSocket, mock |
| `IAudioBackend` | `src/audio/IAudioBackend.h` | internal DAC, MAX98357A, PCM5102A, ES8388, WM8960, TAS5760M, null, mock |
| `IAudioEngine` | `src/audio/IAudioEngine.h` | `AudioEngine` |
| `IValveActuator` | `src/valves/IValveActuator.h` | servo (LEDC), servo (PCA9685), solenoid, mock |
| `ISpeakerProfile` | `src/audio/Profiles.h` | table-driven `SpeakerProfile` |
| `IAmplifierProfile` | `src/audio/Profiles.h` | table-driven `AmplifierProfile` |

## Repository layout

```
platformio.ini          three embedded environments + a host test environment
partitions/             partition tables (4 MB single app, 8 MB dual OTA)
src/
  main.cpp              setup() / loop(), telemetry only
  core/
    AppController.*     boot sequence, FreeRTOS tasks, global actions
    EventBus.*          fan-out for state changes and faults
    SystemState.*       run mode, faults, counters
    Platform.h          the shim that lets logic compile on a PC
    RingBuffer.h        lock-free SPSC queue
  config/
    ConfigTypes.h       the whole schema as plain data
    ConfigManager.*     LittleFS persistence, atomic save
    ConfigSerializer.*  JSON <-> struct
    ConfigMigration.*   old files upgraded in place
    ConfigValidator.*   GPIO conflicts, board capabilities, safety
    BoardCaps.*         what this silicon can really do
    Presets.*           the documented hardware bundles
  midi/
    MidiMessage.h  MidiParser.*  MidiRouter.*  NoteStack.*
    MidiBle.*  MidiUsb.*  MidiDin.*  MidiRtp.*  MidiWebSocket.*
  audio/
    AudioEngine.*  Oscillator.*  AdditiveSynth.*  WavetableSynth.*
    Envelope.*  Filters.*  Limiter.*  Profiles.*  AudioBackendFactory.*
    backends/  Esp32Dac.*  Max98357.*  Pcm5102.*  Es8388.*  Wm8960.*
               Tas5760.*  I2sBackendBase.*  NullBackend.*  CodecI2c.*
  valves/
    ValveController.*  FingeringEngine.*  ServoMotion.*  SolenoidSafety.*
    ServoValve.*  Pca9685Valve.*  SolenoidValve.*
  network/
    WifiManager.*  WebServer.*  WebSocketServer.*  CaptivePortal.*
  diagnostics/
    MidiMonitor.*  Logger.h
data/                   the web interface served from LittleFS
test/                   host unit tests + mock drivers
tools/                  host-side helpers
docs/                   this documentation
```

## Tasks and priorities

Audio and MIDI run on the application core; the network stack stays on core 0
with the Wi-Fi driver, so an HTTP request can never delay a note.

| Task | Core | Priority | Period | Job |
|---|---|---|---|---|
| `ot_audio` | 1 | 20 | blocks on the I²S DMA | render one block, convert, write |
| `ot_midi` | 1 | 18 | 1 ms | poll every transport |
| `ot_valves` | 1 | 10 | 5 ms | servo ramps, solenoid guard |
| `ot_net` | 0 | 5 | 2 ms | HTTP, WebSocket, Wi-Fi, captive portal |
| `loop()` | 1 | 1 | 20 ms | telemetry and logging only |

That ordering is the one required by the project brief: audio DMA, then MIDI
processing, then valve control, then the WebSocket, then the HTTP UI, then
logging.

### Crossing between tasks

* **MIDI → audio**: a lock-free `RingBuffer<MidiMessage, 64>`. `onMidi()` runs
  on the MIDI task and only pushes; the audio task drains it at block
  boundaries, at most 32 messages per block so the work stays bounded.
* **MIDI → valves**: a direct call. The valve controller only touches its own
  state and the actuator drivers, which are owned by the actuator task's
  peripherals; the operations are single register writes.
* **Anything → web**: `SystemState` holds plain scalars that the network task
  reads. A torn read of a counter is harmless and no lock is ever taken on the
  audio or MIDI path.

### Real-time rules

Enforced throughout `src/audio` and `src/midi`:

* no `delay()` anywhere in the audio, MIDI or valve path;
* no dynamic allocation on the render path — the two audio buffers are
  allocated once at boot and never touched again;
* no `String` manipulation in a real-time context;
* everything is a fixed-size member: parser buffers, note stack, monitor ring,
  BLE and RTP transmit buffers;
* pitch, brightness and filter coefficients are recomputed at **control rate**
  (every 32 samples, ≈1.5 kHz at 48 kHz), never per sample, so `pow()` and
  `exp()` stay out of the inner loop;
* the audio task's only blocking call is `i2s_channel_write()` on the DMA
  ring, with a 40 ms ceiling — far longer than any legitimate wait, so
  reaching it is counted as an underrun rather than hanging the task.

## Boot sequence

`AppController::begin()` follows the order the brief requires:

```
1. mute audio                    engine starts muted, no driver exists yet
2. release valves                begin() parks every valve before anything else
3. disable solenoids             gates driven to the inactive level first
4. load configuration            LittleFS -> migrate -> sanitise -> validate
5. validate hardware             board capabilities + GPIO conflicts
6. initialise peripherals        valve drivers, then the audio backend
7. start MIDI                    USB, BLE, DIN, WebSocket
8. start network                 Wi-Fi, captive portal, HTTP, WebSocket, RTP
9. unmute audio                  only once everything above succeeded
```

If step 4 or 5 fails the sequence stops and the instrument comes up in
**SAFE MODE**: Wi-Fi AP and the web configuration only, no actuator ever
energised and no audio backend started. The web UI says why, and the stored
configuration is kept so the user can fix it instead of losing their work.

### Watchdog and reset

The ESP32 task watchdog is active. Whatever the cause of a restart:

* solenoid gates are driven to their inactive level **before** the PWM unit is
  configured, so a reset can never leave a coil energised;
* servos are commanded to their released angle as the first thing `begin()`
  does;
* the audio backend comes up muted and is only unmuted at the end of the boot
  sequence.

## Configuration

A single versioned JSON document in LittleFS (`/config.json`), written
atomically: the new file is written to `/config.new`, the old one is moved to
`/config.bak`, then the new one is committed. A power cut in the middle never
leaves an unbootable instrument.

```
load  ->  parse  ->  migrate (v1 -> v2 -> ...)  ->  sanitise  ->  validate
                                                        │            │
                                          repairs what can be    ERROR ->
                                          repaired safely        SAFE MODE
```

`sanitise()` is the last line of defence: it forces a solenoid maximum ON
time back into range, clamps PWM percentages, drops MIDI transports the board
cannot provide and restores a sane sample rate. A stored file is never trusted
blindly.

Migration steps live in `ConfigMigration.cpp`, one function per version
transition. A file written by a *newer* firmware is refused rather than
guessed at, and the instrument boots in SAFE MODE with an explicit message.

## Status

### Implemented and building for both targets

Configuration + migration + validation, hardware presets, MIDI router with
per-route filters, USB-MIDI (S3), BLE MIDI, DIN MIDI in/out/thru, RTP-MIDI,
WebSocket MIDI, additive / wavetable / hybrid / sine synthesis, ADSR with
attack and breath noise, vibrato, pitch bend, monophonic note priority with
legato / retrigger / portamento, seven audio backends, speaker and amplifier
profiles, speaker protection and limiter, acoustic coupling profiles, servo
(LEDC and PCA9685) and solenoid drivers with thermal protection, mixed
configurations, fingering engine with instrument transposition, PANIC, safe
mode, diagnostics, MIDI monitor, REST API, WebSocket telemetry, OTA, and the
complete web interface.

### Prepared, deliberately not finished

These are named here, in the web UI and in the code, and are never presented
as working:

| Item | State |
|---|---|
| `SAMPLE` sound generator | Not implemented. The enumeration does not contain it and the UI does not offer it, because the storage backend (Flash / LittleFS / SD / PSRAM) is not written. The generator interface is ready for it. |
| Microphone acoustic calibration | The ES8388 and WM8960 backends initialise their ADC and the I²S capture channel is opened, so the signal path exists. The sweep, the measurement and the EQ correction are **not** implemented. The Calibration page says so. |
| MPE, SysEx | Program Change selects a saved voicing when the user turns the option on. SysEx is reassembled and routed; the sound engine ignores it. MPE is not implemented — per-note pitch needs a polyphonic voice allocator. |
| ES8388 / WM8960 / TAS5760M | Written from the datasheets and compiled for both targets, not validated on silicon by the project. Reported as `EXPERIMENTAL` in the UI and the diagnostics. |
| OTA on a 4 MB ESP32 | Not possible — the firmware does not leave room for two OTA slots. Detected at runtime and reported, not hidden. |

### V1 acceptance criteria

| # | Criterion | Where it is covered |
|---|---|---|
| 1 | Hotspot starts | `WifiManager::startAp`, AP fallback |
| 2 | Web interface reachable | `HttpServerModule`, captive portal |
| 3 | Configuration is saved | `ConfigManager::save`, atomic, tested |
| 4 | A hardware profile can be selected | `Presets`, `/api/config/preset`, tested |
| 5 | The web keyboard plays a note | `MidiWebSocketTransport` → router |
| 6 | BLE MIDI triggers a note | `MidiBleTransport` |
| 7 | DIN MIDI triggers a note | `MidiDinTransport` |
| 8 | USB MIDI works on the S3 | `MidiUsbTransport`, TinyUSB MIDI class |
| 9 | The additive engine works | `AdditiveSynth`, tested |
| 10 | PCM5102A works | `Pcm5102Backend` |
| 11 | MAX98357A works | `Max98357Backend` |
| 12 | Three valves can be driven | `ValveController`, tested |
| 13 | Servos work | `ServoValve`, `ServoMotion`, tested |
| 14 | Solenoids work | `SolenoidValve`, `SolenoidSafety`, tested |
| 15 | A mixed servo/solenoid setup works | tested (`test_controller_mixed_servo_and_solenoid`) |
| 16 | Automatic fingering works | `FingeringEngine`, tested |
| 17 | All Notes Off releases every valve | tested (`test_controller_all_notes_off_releases_every_valve`) |
| 18 | PANIC returns to a safe state | tested (`test_controller_panic_parks_everything`) |
| 19 | A solenoid fault prevents overheating | tested (`test_solenoid_releases_on_a_stuck_note`) |
| 20 | The web server causes no audible dropout | Separate cores, priority ordering, DMA depth, underrun counter on the Diagnostics page |

Criteria 5–8, 10, 11 and 20 depend on physical hardware and are validated on
the bench, not by the host test suite. Everything the host *can* verify is
verified: `pio test -e native` runs 102 tests over the configuration, the MIDI
parser and router, the sound engine driven end to end through MIDI, and the
valve logic.

## Development rules

Never:

* add a web option that is not wired to a real function;
* create a fake driver just to make an option appear;
* ignore a compilation error;
* leave a partially working backend labelled as stable;
* use a blocking delay in the audio or MIDI path;
* hard code three pistons into the MIDI engine;
* hard code PCM5102A as the only output;
* hard code ESP32-S3 as the only board.

Always:

* separate interface, logic and drivers;
* check return values;
* keep the hardware in a safe state;
* comment the choices that matter;
* document the limitations;
* validate the GPIO;
* protect the speaker and the solenoids;
* re-test the existing configurations after a change.
