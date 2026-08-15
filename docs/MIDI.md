# MIDI

## Transports

Five interfaces, one interface class (`IMidiTransport`). The router treats
them identically, which is what lets you swap BLE for DIN without touching the
sound engine.

| Port | In | Out | Availability |
|---|---|---|---|
| `USB` | ✔ | ✔ | ESP32-S3 only |
| `BLE` | ✔ | ✔ | any ESP32 with BLE |
| `RTP` | ✔ | ✔ | any board, once Wi-Fi is up |
| `DIN` | ✔ | ✔ + THRU | any board |
| `WEB` | ✔ | ✔ | the browser keyboard and monitor |

### USB-MIDI (ESP32-S3)

Class compliant, through the TinyUSB MIDI class shipped with Arduino-ESP32
3.x. The trumpet enumerates as a standard USB-MIDI device on Windows, macOS,
Linux, a Raspberry Pi, a DAW or a hardware sequencer, with **no driver**.

The `esp32-s3-devkitc` environment sets `ARDUINO_USB_MODE=0` so the USB
peripheral belongs to TinyUSB rather than to the hardware CDC.

On a plain ESP32 there is no native USB peripheral and no TinyUSB MIDI class
in the SDK, so `BoardCaps::hasNativeUsb` is false, the validator rejects the
option and the web UI hides it. A USB *serial* bridge would be a different
thing entirely and this project does not present one as class-compliant
USB-MIDI.

### BLE MIDI

The standard MMA/Apple "MIDI over Bluetooth LE" profile:

```
service         03B80E5A-EDE8-4B33-A751-6CE34EC4C700
characteristic  7772E5DB-3868-4112-A1A9-F2669D106BF3   write-without-response | notify
```

Implemented directly on the Arduino-ESP32 BLE stack rather than through a
third-party library — the profile is one service with one characteristic and a
five-byte timestamp header, and owning it keeps the build reproducible.

The Bluetooth name is configurable (default `GMB MIDI Trumpet`). Advertising
restarts automatically on disconnect, so the instrument never needs a power
cycle to be found again. Outgoing messages are batched and flushed once per
MIDI task cycle: a chord becomes one notification instead of one per note.

### RTP-MIDI / AppleMIDI

Two UDP sockets, control on 5004 and data on 5005 by default. The instrument
is always the **responder**: a DAW, the Windows rtpMIDI driver or the macOS
Audio MIDI Setup invites it. One session at a time, which is what a
monophonic instrument needs; a second initiator is politely refused rather
than silently stealing the stream.

Implemented in-tree: invitation `IN`/`OK`/`NO`, end of session `BY`, clock
synchronisation `CK0` → `CK1`, and the RTP-MIDI payload (payload type 97) with
its command-section header and delta times. The recovery journal is not
implemented — on a local network the packet loss it protects against is rare,
and a missing journal degrades to a dropped message rather than to corruption.

The session is advertised over mDNS as `_apple-midi._udp`, so it appears by
name instead of by IP address.

### DIN MIDI

31250 baud, 8N1, on a hardware UART. THRU is byte-level and happens *before*
parsing, so it stays transparent even for messages the firmware does not
understand.

The input must be opto-isolated on the hardware side — see
[HARDWARE.md](HARDWARE.md#4-din-midi). Reading is bounded to 64 bytes per poll
so a flood on the wire can never starve the rest of the MIDI task.

### WebSocket MIDI

The browser keyboard is an ordinary transport: the router does not know the
notes come from a browser. It is deliberately **not** coupled to the RTP-MIDI
transport — they are two independent modules that happen to both use the
network.

---

## Router

```
   sources                              destinations
   ────────                             ────────────
   USB  ─┐                          ┌─► SOUND ENGINE
   BLE  ─┤                          ├─► VALVE ENGINE
   RTP  ─┼──►  MIDI ROUTER  ────────┼─► USB OUT
   DIN  ─┤                          ├─► BLE OUT
   WEB  ─┘                          ├─► RTP OUT
                                    ├─► DIN OUT
                                    └─► WEB MONITOR
```

Each route carries its own:

* **channel filter** — a 16 bit mask, on top of the global mask;
* **transpose** — −48…+48 semitones;
* **velocity curve** — `LINEAR`, `SOFT`, `HARD`, `FIXED`;
* **note range** — `noteMin`…`noteMax`.

Out of the box every input drives both engines: plug anything in and the
trumpet plays. Hardware outputs start disabled so no unexpected loop can form
with a DAW.

### Loop suppression

Two mechanisms:

* a route whose destination equals its source is skipped when
  `suppressLoops` is on (the default);
* the router refuses to re-enter itself while it is dispatching, so a
  destination transport can never feed a message back into the same pass.

Both are counted and shown on the Diagnostics page.

### Velocity curves

| Curve | Shape | Use |
|---|---|---|
| `LINEAR` | `v` | pass through |
| `SOFT` | `v(2−v)` | compressive — easier to reach loud dynamics from a light keyboard |
| `HARD` | `v²` | expansive — more control in the quiet range |
| `FIXED` | a constant | sequencers that do not send meaningful velocities |

A velocity of 0 is never turned into a note-on, whatever the curve: a Note On
with velocity 0 *is* a Note Off and must stay one.

### PANIC

Reachable from the web UI, over MIDI (CC 120 / CC 123) and through
`POST /api/panic`. It sends All Sound Off, Reset Controllers and All Notes Off
to both engines **and to every enabled output**, so a synthesiser chained
behind the trumpet stops too.

---

## Supported messages

| Message | Behaviour |
|---|---|
| Note On / Note Off | Drives the note stack. Note On with velocity 0 is a Note Off. |
| CC 1 — Modulation | Vibrato depth, when the vibrato source is set to CC1. |
| CC 2 — Breath | Continuous air supply: takes over from the note velocity for both loudness and brightness, which is what a breath controller player expects. |
| CC 7 — Volume | Channel volume. |
| CC 11 — Expression | Scales the level and tilts the harmonic content. |
| Pitch Bend | ±1, ±2, ±3 or ±12 semitones (default ±2). |
| Channel Pressure | Alternative vibrato source, and a small contribution to brightness. |
| CC 120 — All Sound Off | Immediate silence, notes cleared, valves released. |
| CC 121 — Reset Controllers | Controllers back to their defaults. |
| CC 123 — All Notes Off | Notes released, valves released. |
| Real-time (clock, start, stop, …) | Parsed and routed; the engines ignore them. |
| Program Change, SysEx | Parsed and routed; the sound engine ignores them. The parser has a 256 byte SysEx buffer and counts what it had to drop. |
| MPE | Not implemented. The architecture is extensible — per-note pitch would require a polyphonic voice allocator — but nothing pretends to support it today. |

---

## Monophonic behaviour

A trumpet plays one note at a time, but a keyboard or a DAW will happily send
overlapping Note Ons. The note stack keeps every held note and answers "which
one should sound now" according to the configured priority. Both the sound
engine and the valve engine use the same stack, so they can never disagree
about the current note.

| Priority | Winner |
|---|---|
| `LAST` (default) | the most recently pressed |
| `HIGH` | the highest |
| `LOW` | the lowest |

Twelve notes can be held at once; a thirteenth drops the oldest, because on a
monophonic instrument the newest key is always the one the player means.

### Articulation

| Setting | Effect |
|---|---|
| `legato` | Overlapping notes glide into the new pitch without re-articulating the envelope. |
| `retrigger` | Every note restarts the envelope and resets the oscillator phases, even under legato. |
| `portamento` | 0–400 ms glide between pitches. 0 means an instant jump. |

A note starting from silence always restarts the envelope, whatever the
settings — otherwise the first note of a phrase would be inaudible.

---

## Instrument transposition

Transposition is never hard coded. The fingering engine converts an incoming
MIDI note to **written** pitch to look up a fingering, and to **sounding**
pitch to drive the synthesis.

| Instrument | written − concert |
|---|---|
| B♭ trumpet (default) | +2 |
| C trumpet | 0 |
| E♭ trumpet | −3 |
| Custom | −24…+24, your choice |

And the incoming stream can be interpreted either way:

* **Concert pitch** — the note you send is the note you hear. Written pitch is
  derived for the fingering. This is what a DAW normally produces.
* **Written pitch** — the note you send is what a player would read. The
  sounding pitch is derived from it. This is what you want when driving the
  instrument from a trumpet part.

---

## MIDI monitor

A 128-entry ring on the device, pushed to the browser over the WebSocket in
batches of at most twelve. Timestamp, source, type, channel and both data
bytes, with pause, clear and per-source/per-type filters. Bounded on purpose:
a stuck sequencer can send thousands of messages per second and the monitor
must never be able to exhaust the heap. Overflowed entries are counted and
reported rather than hidden.
