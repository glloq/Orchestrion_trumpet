# Web interface

Plain HTML, CSS and vanilla JavaScript served from LittleFS. No framework, no
web font, no CDN — the instrument must be fully configurable from a phone with
no internet connection, over its own hotspot.

## Access

| Situation | Address |
|---|---|
| First start, or no Wi-Fi configured | `http://192.168.4.1` on the `MIDI-Trumpet-XXXX` hotspot |
| Joined your network | `http://midi-trumpet.local` (mDNS), or the IP shown in the side menu |

A captive portal answers every DNS query while the hotspot is up, so a phone
usually opens the configuration page by itself. It can be turned off in
Advanced.

If the configured network cannot be joined within 12 seconds the firmware
falls back to the hotspot automatically, and keeps retrying the network in the
background. You are never locked out.

## Pages

```
Dashboard   Play   MIDI   Instrument   Audio   Pistons
Hardware    Calibration   Diagnostics   Advanced   Firmware
                    Setup wizard
```

The everyday pages never mention a GPIO or an I²S clock. Everything rare or
dangerous lives on **Advanced**.

| Page | What it is for |
|---|---|
| **Dashboard** | Current note, velocity, frequency, audio state and level, valve state, MIDI connections, faults. Nothing else. |
| **Play** | Two-octave keyboard with octave shift, velocity, pitch bend, modulation, breath, expression and volume. Live valve display. |
| **MIDI** | Which interfaces are on, the routing matrix, live per-port counters, and the MIDI monitor. |
| **Instrument** | Transposition, note priority, legato / retrigger / portamento, range, and the editable fingering table. |
| **Audio** | Generator, envelope, vibrato, and the DAC → amplifier → speaker → coupling chain. |
| **Pistons** | Valve count, mode, and per-valve actuator configuration. Live state, manual control, test pulses. |
| **Hardware** | Detected board, one-click presets, the output chain, and the validation report. |
| **Calibration** | Servo angles that move as you drag, audio tones and sweep, EQ and high pass, hardware tests. |
| **Diagnostics** | Firmware, memory, audio, MIDI, network, valves, faults. Refreshes every 4 s. |
| **Advanced** | Pin assignments, I²C addresses, UART, sample rate, bit depth, block size, DMA, limiter internals, servo and solenoid timing, network, per-route MIDI filters, import/export, factory reset. |
| **Firmware** | Version, OTA upload with progress, reboot. |

### Setup wizard

Ten steps, exactly as the project brief lays them out:

```
1  Board                  detected, not chosen
2  Audio backend          only what this chip supports
3  Amplifier
4  Speaker
5  Valve actuator         per-valve servo or solenoid
6  MIDI interfaces
7  GPIO validation        ERROR / WARNING / INFO
8  Audio test             tone and sweep
9  Valve calibration      moves the servo as you drag
10 Save                   validate, write, reboot
```

Nothing is written to the instrument until step 10. Step 1 also offers the
documented presets, which fill the whole audio chain in one click; applying
one goes through the device so the numbers come from a single source of truth
(the C++ preset table) rather than being duplicated in JavaScript.

The wizard opens automatically on a board that has never been configured.

## Visual language

Cards, toggles, dropdowns, sliders and status badges. Connection states are
always one of `Connected` / `Disconnected` / `Warning` / `Fault`, with the same
colour everywhere. The layout is responsive down to 390 px and has no
horizontal scroll; wide content (tables, the keyboard) scrolls inside its own
container.

The interface follows the browser's light/dark preference.

## Validation before saving

Every save is checked first, and the result is shown inline:

```
ERROR    the save is blocked
WARNING  the save goes through, the issue is reported
INFO     informational
```

What is checked is listed in
[HARDWARE.md § GPIO validation](HARDWARE.md#9-gpio-validation).

Examples the UI reacts to automatically:

```
MAX98357A + 4 Ω speaker            → OK
TPA3118 + 5 W speaker              → WARNING + limiter derating shown
USB MIDI + ESP32-WROOM             → NOT AVAILABLE (option hidden)
Internal DAC + ESP32-S3            → NOT AVAILABLE (option hidden)
```

## REST API

All responses are JSON and carry `"ok"`.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/status` | Everything the dashboard needs |
| `GET` | `/api/diagnostics` | Full diagnostics |
| `GET` | `/api/hardware` | Board capabilities, backends, amplifiers, speakers, presets |
| `GET` | `/api/config` | The current configuration |
| `PUT` `POST` | `/api/config` | Validate, save, report issues |
| `POST` | `/api/config/validate` | Validate without saving |
| `GET` | `/api/config/export` | Download as a file |
| `POST` | `/api/config/import` | Import, through the same validation |
| `POST` | `/api/config/preset` | Apply a hardware preset |
| `POST` | `/api/panic` | Immediate safe state |
| `POST` | `/api/panic/release` | Leave the panic state |
| `POST` | `/api/audio/test` | `{"type":"tone"\|"sweep"\|"stop", …}` |
| `POST` | `/api/audio/mute` | `{"muted":true}` |
| `POST` | `/api/audio/volume` | `{"volume":0.0–1.0}` |
| `POST` | `/api/valve/test` | `{"valve":0,"durationMs":300}` |
| `POST` | `/api/valve/mode` | `{"mode":"AUTO"}` |
| `POST` | `/api/valve/manual` | `{"valve":0,"pressed":true}` |
| `POST` | `/api/valve/calibrate` | `{"valve":0,"angle":88}` — moves immediately |
| `GET` | `/api/midi/status` | Ports, routes, counters |
| `GET` | `/api/midi/monitor` | The monitor buffer |
| `GET` `PUT` | `/api/fingering` | Read / write the table |
| `POST` | `/api/fingering/reset` | Back to the standard chart |
| `POST` | `/api/system/reboot` | Park, then restart |
| `POST` | `/api/system/factory-reset` | Erase and restart |
| `POST` | `/api/ota` | Multipart firmware upload |

A rejected save answers `422` with the issue list:

```json
{
  "ok": false,
  "rebootRequired": false,
  "issues": [
    { "severity": "ERROR", "field": "valves.1",
      "message": "GPIO 5 is already used by I2S BCLK" }
  ]
}
```

A valve test is capped at 2 seconds by the firmware, whatever the request
asks for, so a test can never cook a coil.

## WebSocket

`ws://<host>:81/` — everything that must be immediate.

### Browser → trumpet

```json
{"t":"noteon","n":60,"v":100,"c":1}
{"t":"noteoff","n":60,"c":1}
{"t":"cc","n":2,"v":64,"c":1}
{"t":"pb","v":4096,"c":1}
{"t":"valve","i":0,"p":true}
{"t":"panic"}
{"t":"monitor","paused":false,"clear":true}
```

### Trumpet → browser

```json
{"t":"telemetry","mode":"RUNNING","uptime":3725,
 "audio":{"running":true,"muted":false,"underruns":0,"cpu":18.4,"peak":0.42,
          "gainReduction":1.0,"note":64,"velocity":96,"frequency":329.6,
          "backend":"PCM5102A","sampleRate":48000,"volume":0.75},
 "midi":{"rx":6,"tx":0,"usb":true,"ble":false,"din":true,"rtp":false,"web":1},
 "valves":[{"pressed":true,"type":"SERVO","angle":88,"duty":0,"fault":false}],
 "valveMode":"AUTO","faults":0}

{"t":"monitor","items":[{"ts":12345,"src":"DIN","type":"Note On",
                         "ch":1,"d1":60,"d2":100}]}
```

Telemetry is pushed every 200 ms and the monitor every 120 ms, in batches of
at most twelve entries. The keyboard sends notes over this socket rather than
through individual HTTP requests, which is what makes it feel immediate.

The client reconnects on its own with a gentle back-off, because a hotspot
drops whenever a phone sleeps.

## Real-time isolation

The HTTP and WebSocket servers run in the network task, on core 0, at priority
5 — below audio (20), MIDI (18) and the actuators (10), and on the other core.
An HTTP request cannot delay a note. If you want to see that for yourself, the
underrun counter on the Diagnostics page is the measurement: browse the UI
while playing and watch it stay at zero.

## Serving

Files are served from LittleFS, with pre-compressed `.gz` variants preferred
when present. Upload them with:

```bash
pio run -e esp32-s3-devkitc -t uploadfs
```

If the filesystem is empty the server still answers with a short page telling
you to run that command, rather than a blank 404.
