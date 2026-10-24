# Slice75 HE USB protocol notes

These notes describe the configuration protocol of the Chilkey Slice75 HE as understood for this project.
They were worked out by observing how the vendor's browser configurator talks to the keyboard, for the sole
purpose of interoperability. Nothing here is official. Items marked **unverified** have not been confirmed on
hardware.

All multi-byte values are little-endian unless stated otherwise. Distances are in micrometres (µm).

## Transport

- HID interface with usage page `0xFFA0`, usage `0x01`. Known IDs: `1CA3:0701` (Slice75 HE), `1CA5:0701`,
  `1CA3:0D01`, `1CA3:0703`. Other brands reuse these IDs; their product strings are rejected.
- Output and input reports are 64 bytes with report ID 0.

### Packet layout

| byte | meaning |
|---|---|
| 0 | `0x5C` |
| 1 | payload length `n` |
| 2 | command |
| 3 | check byte |
| 4 … 3+n | payload |

The check byte is `(0x35 + b0 + b1 + b2 + b[3+n]) & 0xFF`: it covers the header and only the **last**
payload byte.

Replies use the same layout with the command's high bit set (`cmd | 0x80`). The first payload byte of a reply
is a status / read-write echo. Live-travel replies (`0x92`) span three consecutive reports (192 bytes).

## Commands

| cmd | name | purpose |
|---|---|---|
| `0x00` | CMD | sub-commands below |
| `0x01` | SYNC | identity, firmware string, run mode |
| `0x12` | RM6X21 | live travel / ADC stream |
| `0x18` | PRGB | main backlight |
| `0x19` | LOGORGB | ambient / logo light (absent on the Slice75 HE) |
| `0x20` | MACRO | macro body |
| `0x21` | MACROMODE | macro binding, mode and repeat |
| `0x23` | KEY | per-key fields (keymap and performance) |
| `0x24` | MT | mod-tap |
| `0x25` | TGL | toggle |
| `0x26` | DKS | dynamic keystroke |
| `0x27` | MPT | three-depth trigger |
| `0x28` | END | send a key on release |
| `0x29` | DB | global actuation and deadzones |
| `0x2A` | KRGB | per-key colour |
| `0x2B` | DEFKEY | key matrix |
| `0x2C` | SOCD | opposing-key resolution |
| `0x2D` | RS | "deeper key wins" pair |

### `CMD` (0x00) sub-commands

Payload is `[order, param?, 0xFF, 0xFF]`. A query sends an out-of-range parameter (or none); a set sends the
value. The reply carries the order at payload byte 1 and the value from byte 2.

| order | meaning | query / set |
|---|---|---|
| 1 | protocol version: byte 3 low nibble = major, byte 2 = minor (high nibble) and patch (low nibble) | query |
| 12 / 13 | start / save calibration | set |
| 17 | factory reset | set |
| 33 / 34 | Windows / macOS mode query | query |
| 37 | precision (u8 µm step), min travel (u16), max travel (u16) | query |
| 38 | keyboard name (32 bytes) | query |
| 39 | case colour: 0 black, 1 gray, 2 white, 3 all black, 4 pink | param 100 |
| 40 | default switch | param 100 |
| 48 / 49 | switch to Windows / macOS | set |
| 52 | top deadzone switch | param 2 = query, 0/1 = set |
| 80 | polling rate index (8K, 4K, 2K, 1K, 500, 250, 125 Hz) | param 7 = query |
| 112 | active profile 0–3 | param 4 = query |
| 118 | supported switch list: up to 8 **big-endian** u16 ids | query |

Feature availability depends on the protocol version: per-key deadzones ≥ 1.0.1, per-key switch type ≥ 1.0.2,
16-bit keycodes in advanced keys ≥ 1.0.3, macro v2 ≥ 1.0.4, SOCD v3 ≥ 1.0.5, top deadzone switch ≥ 1.0.6.
The tested Slice75 HE reports protocol 1.0.7 and firmware "App V1.1.7".

### SYNC (0x01)

Request payload `01 02 03 04 FF FF`. In the reply (offsets into the payload): board id u32 at 1–4 (byte 3 is
the switch-family "key type", byte 4 the layout id; the Slice75 HE uses layout 31), run mode at 7 (`0xFF` =
bootloader), 16-byte serial at 9, firmware string at 26.

### DEFKEY (0x2B) — key identities

Request `[0, rowA, rowB]` for row pairs (0,1), (2,3), (4,5) of the 6 × 21 matrix. The reply lists, per row,
the row number followed by 21 bytes. Each non-zero byte is the key's **key id**: its default HID usage code.
All per-key commands address keys by this id, never by row/column.

### KEY (0x23) — per-key fields

Payload: `[rw]` then up to 14 records `[key id, field, value u16]` (0 = read, 1 = write). The reply has the
same layout. Fields:

| field | meaning |
|---|---|
| 0–3 | keycode on layer Fn0–Fn3 |
| 4 | actuation point (µm) |
| 5–7 | advanced-key depths (µm) |
| 8 | mode: `touchMode << 4 \| advancedType` (touch: 0 global, 1 per-key, 2 rapid trigger; advanced: 0 none, 1 DKS, 2 MPT, 3 MT, 4 TGL, 5 END, 6 macro, 8 SOCD, 9 RS) |
| 9–12 | advanced-key keycodes |
| 13–16 | DKS trigger masks |
| 17 / 18 | macro address / size (macro v1) |
| 19 | MT/TGL hold time (10 ms units) |
| 20 / 21 | rapid trigger press / release sensitivity (µm) |
| 22 / 23 | press / release deadzone (µm) — which physical end each one is: **unverified** |
| 24 | release point (µm) |
| 25 | switch type, as an index into the supported switch list |

### DB (0x29) — global values

Payload `[rw, 0, 0, actuation u16, deadzonePress u16, deadzoneRelease u16, 0 × 6]`. The reply mirrors it.
Keys whose touch mode is "global" use this actuation point.

### Keycodes

16-bit values: `0x0004–0x00E7` HID keyboard usages; `0x1000 | usage` consumer-page keys (volume, media,
launchers); `0x7301–0x7309` mouse buttons, movement and wheel; `0xF001–0xF003` Fn1–Fn3; `0xF1xx` system
actions (factory reset, Win/Mac, calibration, profiles, WASD swap — held for 3 s); `0xF3xx` lighting and
Win-lock controls. The full table is in `src/keycodes_table.cpp`.

### Lighting

**PRGB (0x18)** and **LOGORGB (0x19)** share one 43-byte payload:

| offset | meaning |
|---|---|
| 0 | 0 read, 1 write |
| 5–32 | 7 colours, each as `B, G, R, 0xFF` |
| 37 | flags: bit 0 on, bit 1 reverse, bit 4 Super Response (a latency setting stored here) |
| 38 | brightness 0–4 |
| 39 | mode: 0 static, 1–20 effects, 21 per-key |
| 40 | speed 0–4 |
| 41 | sleep timeout in minutes (0 = never) |
| 42 | selected static colour slot 0–7 (7 = built-in gradient, **unverified**) |

Every write carries the complete state. LOGORGB replies only exist on boards with an ambient light.

**KRGB (0x2A)**: `[rw]` then up to 14 entries `[key id, R, G, B]` (note: RGB order). Unused bytes are
`0xFF`. Colours are shown in mode 21. Whether they survive a power cycle is **unverified**.

### Advanced keys

Write-only packets that start with `[1, key id]`; their state is read back through KEY fields 5–19. With
protocol ≥ 1.0.3 keycodes are u16, otherwise u8.

| cmd | body after the key id |
|---|---|
| DKS `0x26` | 4 keycodes, 4 trigger masks (u8), depth 1 u16, depth 2 u16 — read back as field 5 = depth 1, field 7 = depth 2, field 6 = their midpoint (verified) |
| MPT `0x27` | 3 keycodes, 3 depths u16 |
| MT `0x24` | hold keycode, tap keycode, hold time (u8, 10 ms units) |
| TGL `0x25` | keycode, hold time (u8, 10 ms units) |
| END `0x28` | keycode |
| RS `0x2D` | partner id, then the mirrored pair `[partner, key]` |

DKS trigger mask bits (**unverified** meaning): `0x01` press past depth 1, `0x02` held between, `0x04` press
past depth 2, `0x18` held at bottom, `0x20` release from depth 2, `0x40` held between, `0x80` release past
depth 1.

To remove an advanced key, write field 8 with the advanced type cleared.

**SOCD (0x2C)**: v1 `[1, a, b, b, a]`; v2 `[1, a, b, m0, b, a, m1]` where the mode index 0–3 selects
`(m0, m1)` from `(0,0) (0,1) (1,0) (1,1)`; v3 `[rw, key1, key2, value1 u16, value2 u16, bindType, mode]`
with mode 0 last input, 1 first key priority, 2 second key priority, 3 neutral.

### Macros

Macro bodies cannot be read back, so the application keeps its own library.

- **Body (0x20)**: `[1, address u16, count]` then events. Address is `256 + index of the first event`.
  - v1: up to 14 events of `[keycode u16, (flag << 12) | delay]`.
  - v2: up to 9 events of `[keycode u16, (flag << 24) | delay]`.
  - flag 1 = key down, 8 = key up; delay is in ms, before the event (**unverified**).
- **Binding (v2)**: write KEY field 8 with advanced type 6, then MACROMODE `[1, key id, macro id u16,
  event count, mode 0–3, repeat count u16, repeat interval u24]`. Modes: run once, toggle repeat, repeat while
  held (stop at once), repeat while held (finish the run).
- **Binding (v1)**: KEY fields 8 (type 6), 17 = 256, 18 = event count.

### Live travel and calibration (0x12)

Request `[sub, half, 0xFF, 0xFF]`; `half` 1 = rows 0–2, 2 = rows 3–5. Sub 2 returns travel in µm (u16 per
matrix cell), sub 6 raw ADC values, sub 3 a u8 press state for all 6 rows. The reply does not say which half
it contains, so the host must remember what it asked for.

Calibration: send CMD 12, stream sub 2 + sub 6 for alternating halves while the user presses every key fully,
then send CMD 13 to save.
