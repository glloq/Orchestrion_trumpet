# Hardware

Everything here is a *recommended* wiring. The firmware never assumes a pin:
every GPIO is configurable from the web UI and validated before it is saved.

---

## 1. Power architecture

```
        24 V PSU
           │
           ├─────────────► amplifier (TPA3118D2)
           │
           ├─────────────► solenoids, if they are 24 V types
           │               (otherwise a second rail, see below)
           │
           └── buck converter ──► 5 V
                                   ├── ESP32 board
                                   ├── DAC / codec board
                                   └── servos or the PCA9685 servo rail
```

**Rules that are not negotiable:**

* **Never power a motor from the ESP32 regulator.** A servo stalling pulls
  more than a metre of USB cable can deliver, the 3V3 rail collapses and the
  board browns out mid-note.
* One **common ground**, star-wired back to the supply. The audio ground and
  the actuator ground must meet at one point only, at the supply — not through
  the ESP32.
* A **bulk capacitor** close to the actuators: 1000 µF or more on the servo
  rail, 2200 µF or more on the solenoid rail.
* **Local decoupling** at every board: 100 nF next to each supply pin.
* Keep the servo and solenoid wiring physically away from the I²S lines and
  the analogue audio wiring. Servo PWM noise injected into the DAC ground is
  the most common cause of a "buzzing" build.

### Typical budget

| Rail | Consumer | Current |
|---|---|---|
| 24 V | TPA3118D2 at the limiter ceiling | ~1 A peak |
| 24 V | 3 solenoids, pull-in | 3 × 1–2 A for 50 ms |
| 5 V | ESP32-S3 with Wi-Fi + BLE | 250 mA average, 500 mA peak |
| 5 V | 3 hobby servos, moving | 3 × 0.5–1 A |
| 5 V | PCM5102A board | 20 mA |

Size the buck converter for the 5 V total, not the average: 5 A is a sensible
figure for a three-servo instrument.

---

## 2. Reference pin map

Both maps are the firmware defaults. Change them from **Advanced** if your
board differs; the validator refuses duplicates, flash pins, input-only pins
and warns on strapping pins.

### ESP32-S3 (`esp32-s3-devkitc`)

| Function | GPIO | Notes |
|---|---|---|
| I²S BCLK | 5 | |
| I²S WS / LRCK | 6 | |
| I²S DOUT | 7 | to the DAC's DIN |
| I²S DIN | — | ES8388 / WM8960 only |
| I²S MCLK | — | only if the codec needs it |
| I²C SDA (codec / PCA9685) | 8 | shared bus, different addresses |
| I²C SCL | 9 | |
| DIN MIDI RX | 18 | from the optocoupler |
| DIN MIDI TX | 17 | to the DIN OUT driver |
| Valve 1 | 15 | servo or MOSFET gate |
| Valve 2 | 16 | |
| Valve 3 | 4 | |
| Valve 4 | 2 | |

Avoid 26–32 (SPI flash / PSRAM on N8R8 and N16R8 modules) and treat 0, 3, 45,
46 as strapping pins. 19/20 are the USB D−/D+ lines: using them for anything
else disables USB-MIDI.

### ESP32-WROOM-32 (`esp32dev`)

| Function | GPIO | Notes |
|---|---|---|
| I²S BCLK | 26 | |
| I²S WS / LRCK | 25 | |
| I²S DOUT | 22 | |
| I²C SDA | 21 | |
| I²C SCL | 19 | |
| DIN MIDI RX | 16 | |
| DIN MIDI TX | 17 | |
| Valve 1 | 32 | |
| Valve 2 | 33 | |
| Valve 3 | 27 | |
| Valve 4 | 14 | |

6–11 are the SPI flash and must never be used. 34–39 are input only and cannot
drive anything. 0, 2, 12, 15 are strapping pins: a servo horn resting on GPIO12
at power-up will stop the board booting.

The internal 8 bit DAC, if you use it for a bring-up test, is on GPIO25
(channel 1) or GPIO26 (channel 2) — which is why it conflicts with the I²S map
above and is prototype-only.

---

## 3. Audio chains

### PCM5102A — the reference

```
ESP32 ──BCK──► BCK   PCM5102A
      ──LCK──► LRCK        ├── LOUT ──► amplifier IN L
      ──DIN──► DIN         └── ROUT ──► (unused, mono instrument)
      ──GND──► GND
        3V3 ──► VIN
```

Module strapping pins (they are usually solder jumpers on the breakout):

| Pin | Set to | Why |
|---|---|---|
| FLT | low | normal latency filter |
| DEMP | low | de-emphasis off |
| XSMT | high, **or** a GPIO | soft mute; if wired to a GPIO, set it as the "DAC mute pin" in Advanced and the firmware mutes in hardware |
| FMT | low | I²S format |

### MAX98357A — compact

```
ESP32 ──BCLK──► BCLK   MAX98357A
      ──LRC───► LRC          └── + / − ──► 4 Ω speaker
      ──DIN───► DIN
        5 V  ──► VIN
      ──GPIO─► SD_MODE   (optional, used as a hardware mute)
```

`SD_MODE` also selects the channel through a resistor to ground; leaving the
module's default and driving the pin high/low from a GPIO gives a clean mute.
The part is 16 bit on the common breakouts, and the firmware reduces the
requested bit depth accordingly and says so in the UI.

### ES8388 / WM8960 — codecs with a microphone input

Same I²S wiring plus:

```
ESP32 ──SDA──► SDA      ES8388 / WM8960
      ──SCL──► SCL
      ──MCLK─► MCLK     (ES8388 usually needs it)
      ◄─DIN─── ADC out
```

The microphone input is what a future acoustic calibration loop will use. In
this firmware version the ADC is initialised and the capture channel is
opened; no measurement is performed.

### TAS5760M — integrated

```
ESP32 ──I2S──► TAS5760M ──► speaker
      ──I2C──►  (optional: volume and mute through registers)
      ──GPIO─► SPK_SD    (hardware mute / shutdown)
```

The part works in hardware control mode with no I²C at all; the driver detects
whether the codec answers on the bus and falls back cleanly.

---

## 4. DIN MIDI

### MIDI IN — must be opto-isolated

```
        DIN-5 socket                     6N138
   pin 4 ──[220 Ω]──────────────► 2 (anode)
   pin 5 ──────────┬──[1N4148]──► 3 (cathode)
                   │   (reverse across the LED)
                   └────────────────────┘

   6N138  6 (VO) ──┬──────────────────► ESP32 RX GPIO
                   └──[270 Ω]──► 3V3
   6N138  8 (VCC) ──► 3V3
   6N138  5 (GND) ──► GND
   6N138  7       ──► GND
```

The MIDI standard requires galvanic isolation on the input: without it a
ground loop through the other device's chassis will inject hum straight into
the audio chain, and a fault on the other device can reach the ESP32. Use a
6N138 or an H11L1; do **not** connect the DIN shield (pin 2) to ground at this
end.

### MIDI OUT

```
   ESP32 TX ──[220 Ω]──► DIN-5 pin 5
   3V3      ──[220 Ω]──► DIN-5 pin 4
   GND                 ► DIN-5 pin 2
```

For a 3.3 V board use 2 × 33 Ω instead of 2 × 220 Ω if you want to stay closer
to the 5 mA loop current the specification expects; 220 Ω works with most
receivers and is safer for the GPIO.

### MIDI THRU

The firmware echoes every received byte on the TX pin before interpreting it,
which is exactly what a hardware THRU port does — including for messages the
firmware does not understand. Enable it on the MIDI page. If you need a real
electrical THRU while MIDI OUT is also in use, add a second buffered socket.

---

## 5. Servos

```
                5 V rail (separate, not the ESP32 regulator)
                    │
                    ├── 1000 µF ── GND
                    │
   servo ──── V+ ───┘
         ──── GND ──────► common ground
         ──── SIG ──────► ESP32 GPIO  or  PCA9685 channel
```

* A 1000 µF bulk capacitor across the servo rail, plus 100 nF at each servo,
  keeps the switching transient out of the audio ground.
* Route the signal wires away from the I²S bus.
* The firmware detaches the PWM once a movement is finished, so the servo
  stops buzzing, stops drawing holding current and stops heating. Turn it off
  in Advanced only if your linkage needs continuous torque.

### PCA9685

```
ESP32 ──SDA──► SDA    PCA9685
      ──SCL──► SCL       ├── V+  ──► servo rail (NOT the logic 3V3)
      ──GPIO─► OE        ├── VCC ──► 3V3 logic
                         └── CH0..CH15 ──► servos
```

`OE` is active low. Wire it and declare it in Advanced: the firmware holds it
high (outputs disabled) before the bus is even configured, so nothing twitches
at power-up, and drives it high again on PANIC.

---

## 6. Solenoids

```
              solenoid rail (12 V or 24 V), fused
                    │
                    ├── 2200 µF ── GND
                    │
                 solenoid
                    │
        ┌───────────┴───────────┐
        │        1N5819         │   flyback diode across the coil,
        └──────────►────────────┘   cathode to the positive rail
                    │
                    ▼ drain
   ESP32 GPIO ──[100 Ω]── gate   logic-level N-channel MOSFET
                              │   (IRLZ44N, IRLB8721, AO3400 for small coils)
                    ┌─────────┴── 100 kΩ ── GND   (gate pull-down)
                    ▼ source
                   GND (common)
```

* The **flyback diode is mandatory**. Without it the inductive kick destroys
  the MOSFET, and often the ESP32 with it.
* The gate pull-down keeps the MOSFET off while the ESP32 is in reset.
* Use a **fuse** sized just above the total pull-in current.
* The MOSFET must be *logic level*: an IRF540 will not turn on properly from
  3.3 V and will cook.

### Firmware-side protection

Configured per valve, on the Pistons page:

| Parameter | Meaning | Typical |
|---|---|---|
| `pullInPwm` | duty during the initial burst | 100 % |
| `pullInMs` | how long the burst lasts | 50 ms |
| `holdPwm` | duty once the armature has moved | 35 % |
| `maxOnMs` | **maximum continuous ON time** | 5000 ms |
| `cooldownMs` | forced rest after a fault | 3000 ms |
| `maxDutyPercent` | long-term duty ceiling | 60 % |

The maximum ON time is not optional: a configuration with `maxOnMs = 0` is
refused by the validator, and a stored file that somehow contains one is
repaired at load. When it fires the coil is released, a fault is raised and
shown in the web UI, and the valve stays cold until the cooldown expires —
even if the MIDI note never ends.

The PWM runs at 20 kHz by default so it stays out of the audio band.

---

## 7. Emergency stop

```
   24 V / 12 V ──────► [ NC mushroom switch ] ──────► actuator rails
                                                       (servos + solenoids)

   5 V logic ─────────────────────────────────────► ESP32  (not cut)
```

Fit a **physical** emergency stop that cuts the actuator supply *without*
going through the firmware. The ESP32 deliberately stays powered so it can
keep serving the web UI and display the fault. This is the one protection that
must work when the firmware does not.

The software `PANIC` is a complement, not a replacement: it mutes the audio,
releases every valve, de-energises the solenoids, stops the servos, clears the
active notes and broadcasts All Sound Off / Reset Controllers / All Notes Off
to every enabled MIDI output. It is reachable from the web UI, over MIDI
(CC 120/123) and through `POST /api/panic`.

---

## 8. Acoustic assembly

```
     speaker
        │
        ▼
  sealed front chamber      ~120 ml for the STANDARD preset
        │
        ▼
  progressive adapter       printed cone, speaker diameter -> ~11 mm
        │
        ▼
  trumpet leadpipe          where the mouthpiece would go
        │
        ▼
     trumpet
```

The speaker is used as a **pressure source feeding the instrument**, not as a
loudspeaker in a box. The chamber must be airtight: any leak turns the
pressure source back into a small open-baffle speaker and the trumpet stops
resonating properly.

* Seal every joint (silicone or a printed gasket).
* Keep the adapter as short and as smooth as you can; steps and sharp corners
  produce audible resonances.
* The chamber raises the effective low-frequency corner, which is why the
  firmware applies a high pass derived from the coupling profile: everything
  below is wasted cone excursion that only heats the coil.

---

## 9. GPIO validation

Before any save, the firmware checks:

* the same GPIO assigned twice, naming both owners;
* flash and PSRAM pins;
* input-only pins used as an output;
* strapping and USB pins (warning, not an error);
* PCA9685 channel collisions;
* an I²C address shared between the codec and the PCA9685;
* UART 0 used for DIN MIDI while it is also the debug console;
* USB-MIDI or BLE requested on a board that does not have the radio;
* the internal DAC requested on a chip that has none;
* a speaker impedance below what the amplifier is rated for;
* an amplifier that can exceed the speaker's rating (warning + limiter);
* servo angles outside 0–180°, an inverted pulse range, an inverted note range;
* solenoid PWM values outside 0–100 %, a hold level above the pull-in level, a
  missing maximum ON time or cooldown.

Results are shown as `ERROR`, `WARNING` or `INFO`. An `ERROR` blocks the save.
