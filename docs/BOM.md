# Bill of materials

Prices are rough 2025 hobby-quantity figures in euros, for orientation only.

---

## Common to every configuration

| Item | Qty | Notes | ≈ € |
|---|---|---|---|
| ESP32-S3 DevKitC-1 (N8R8 or N16R8) | 1 | the reference board | 12 |
| — or ESP32-WROOM-32 DevKit | 1 | no USB-MIDI, 4 MB means no OTA | 7 |
| Trumpet (B♭) | 1 | a student instrument is fine | 80–250 |
| Sealed chamber + printed adapter | 1 | PETG or ABS, airtight | 3 |
| Silicone gasket / sealant | 1 | the chamber must not leak | 4 |
| 5 V buck converter, 5 A | 1 | never power motors from the ESP32 | 6 |
| 24 V PSU, 3–5 A | 1 | amplifier and solenoids | 15 |
| Bulk capacitors 1000 µF / 2200 µF | 2 | actuator rails | 2 |
| 100 nF decoupling | 10 | one per supply pin | 1 |
| Emergency stop, NC mushroom | 1 | **cuts the actuator rails directly** | 8 |
| Fuse holder + fuses | 1 | solenoid rail | 3 |
| Wire, connectors, heatshrink | — | | 8 |

---

## Audio, per preset

### LOW COST — ★★★

```
ESP32 → MAX98357A → Dayton CE70P-4
```

| Item | Qty | ≈ € |
|---|---|---|
| MAX98357A breakout | 1 | 4 |
| Dayton Audio CE70P-4 (4 Ω, 15 W) | 1 | 12 |
| **Audio subtotal** | | **16** |

16 bit, one board, no separate amplifier. The cheapest chain that genuinely
works. The limiter derates it automatically for the 4 Ω driver.

### COMPACT — ★★★

```
ESP32 → MAX98357A → Visaton FRS 5 XTS
```

| Item | Qty | ≈ € |
|---|---|---|
| MAX98357A breakout | 1 | 4 |
| Visaton FRS 5 XTS (8 Ω, 8 W) | 1 | 14 |
| **Audio subtotal** | | **18** |

Smallest chamber (~60 ml). Quiet rooms.

### STANDARD — ★★★★½ — **recommended**

```
ESP32 → PCM5102A → TPA3118D2 → Visaton FRS 8 M → sealed chamber → trumpet
```

| Item | Qty | ≈ € |
|---|---|---|
| PCM5102A breakout | 1 | 5 |
| TPA3118D2 amplifier board (mono or bridged) | 1 | 8 |
| Visaton FRS 8 M (8 Ω, 30 W) | 1 | 22 |
| **Audio subtotal** | | **35** |

The reference chain, used for all audio development. 24 bit into a clean
line-level DAC, a class-D amplifier with enough headroom to never clip, and a
full-range driver that copes with the sealed chamber.

### QUALITY — ★★★★★

```
ESP32 → PCM5102A → TPA3118D2 → Monacor SPX-30M
```

| Item | Qty | ≈ € |
|---|---|---|
| PCM5102A breakout | 1 | 5 |
| TPA3118D2 amplifier board | 1 | 8 |
| Monacor SPX-30M (8 Ω, 30 W) | 1 | 38 |
| **Audio subtotal** | | **51** |

Lower usable corner (110 Hz) and a smoother response than the FRS 8 M.

### FEEDBACK — ★★★★½

```
ESP32 → ES8388 → TPA3118D2 → FRS 8 M / SPX-30M
microphone → ES8388 ADC
```

| Item | Qty | ≈ € |
|---|---|---|
| ES8388 codec board | 1 | 9 |
| TPA3118D2 amplifier board | 1 | 8 |
| Visaton FRS 8 M | 1 | 22 |
| Electret measurement microphone + preamp | 1 | 12 |
| **Audio subtotal** | | **51** |

Prepared for acoustic calibration. The ADC and the capture channel are
initialised; the measurement itself is not implemented in this version — see
[AUDIO.md](AUDIO.md#microphone-calibration--prepared-not-implemented).

### INTEGRATED — ★★★★½

```
ESP32 → TAS5760M → speaker
```

| Item | Qty | ≈ € |
|---|---|---|
| TAS5760M module or a dedicated PCB | 1 | 12 |
| Visaton FRS 8 M | 1 | 22 |
| **Audio subtotal** | | **34** |

One chip replaces the DAC and the amplifier. The target of a future dedicated
board. Reported as `EXPERIMENTAL` until it has been heard.

---

## Valves

### Servo option

| Item | Qty | Notes | ≈ € |
|---|---|---|---|
| MG90S metal-gear servo | 3 | fast, enough torque for a piston | 4 each |
| — or SG90 | 3 | quieter, plastic gears, wears faster | 2 each |
| PCA9685 board | 0–1 | optional, recommended | 4 |
| Printed linkage + horns | 3 | | 2 |
| M2 hardware | — | | 2 |
| 1000 µF on the servo rail | 1 | | 1 |
| **Subtotal** | | | **~20** |

### Solenoid option

| Item | Qty | Notes | ≈ € |
|---|---|---|---|
| Push solenoid, 12 V or 24 V, ~10 mm stroke | 3 | match the piston travel | 8 each |
| Logic-level N-MOSFET (IRLZ44N / IRLB8721) | 3 | **must** be logic level | 1 each |
| 1N5819 flyback diode | 3 | **mandatory** | 0.2 each |
| 100 Ω gate resistor | 3 | | — |
| 100 kΩ gate pull-down | 3 | keeps the gate off during reset | — |
| 2200 µF on the solenoid rail | 1 | | 2 |
| Fuse | 1 | just above the pull-in total | 2 |
| **Subtotal** | | | **~34** |

Mixed configurations are normal: two servos on the fast valves and a solenoid
on the third works, and is covered by the test suite.

---

## MIDI

| Item | Qty | Needed for | ≈ € |
|---|---|---|---|
| DIN-5 socket | 2 | DIN MIDI in and out | 2 each |
| 6N138 or H11L1 optocoupler | 1 | **mandatory** on MIDI IN | 1 |
| 1N4148 | 1 | across the optocoupler LED | 0.1 |
| 220 Ω resistors | 3 | IN and OUT | — |
| 270 Ω resistor | 1 | optocoupler pull-up | — |
| USB-C cable | 1 | USB-MIDI on the S3 | 4 |

BLE MIDI, RTP-MIDI and the web keyboard need no extra hardware at all.

---

## Indicative totals

| Configuration | Electronics, without the trumpet |
|---|---|
| LOW COST + servos | ≈ 110 € |
| **STANDARD + servos** | **≈ 130 €** |
| STANDARD + solenoids | ≈ 145 € |
| QUALITY + servos | ≈ 145 € |
| FEEDBACK + servos | ≈ 145 € |

---

## Electrical limits

| Item | Limit |
|---|---|
| MAX98357A | 5 V supply, 3.2 W into 4 Ω |
| TPA3118D2 | 24 V supply, 25 W into 8 Ω |
| TAS5760M | 24 V supply, ~20 W into 8 Ω |
| PCM5102A | 3.3 V, line level output |
| Visaton FRS 5 XTS | 8 Ω, 8 W RMS — the firmware limits it to 5 W |
| Dayton CE70P-4 | 4 Ω, 15 W RMS — limited to 8 W |
| Visaton FRS 8 M | 8 Ω, 30 W RMS — limited to 20 W |
| Monacor SPX-30M | 8 Ω, 30 W RMS — limited to 22 W |
| ESP32 GPIO | 3.3 V, 12 mA — **never drive a motor directly** |
| Servo rail | 5 V, size for 1 A per servo stalled |
| Solenoid rail | 12 V or 24 V, 1–2 A per coil at pull-in |

The firmware's protection limits are deliberately well below the RMS ratings:
the driver sits in a sealed chamber feeding a leadpipe and never needs its
full excursion. The numbers are per profile and can be changed on the Audio
page, within the validator's rules.

---

## Sourcing notes

* **PCM5102A boards** vary. Check that FLT, DEMP, XSMT and FMT are broken out
  or already strapped correctly (see [HARDWARE.md](HARDWARE.md#audio-chains)).
* **MAX98357A breakouts** are 16 bit in practice. The firmware reduces the
  requested bit depth and says so rather than pretending otherwise.
* **ESP32-S3 modules**: prefer N8R8 or N16R8 for the PSRAM and the room for two
  OTA slots. Avoid the 4 MB variants for this project.
* **Solenoids**: check the stroke against your piston travel *before* ordering.
  A solenoid that cannot complete the travel will sit stalled at full current,
  which is exactly the case the thermal guard exists to survive — but it still
  will not play the note.
