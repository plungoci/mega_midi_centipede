# Mega MIDI Centipede Controller

An **Arduino Mega + MCP23017 (Centipede-style) MIDI controller** firmware that scans up to **64 buttons** and sends **MIDI CC** messages with bank/layer logic, debounce handling, and shift latch behavior.

---

## ✨ Features

- ✅ Up to **64 active-low button inputs** (`TOTAL_PINS = 64`) across detected MCP23017 chips.
- ✅ Automatic I2C scan for MCP23017 devices at addresses **`0x20` to `0x27`**.
- ✅ **Debounced** button scanning (`DEBOUNCE_MS = 20`).
- ✅ Per-button **toggle state** (press to flip ON/OFF).
- ✅ MIDI CC output on **`Serial1` at 31250 baud**.
- ✅ **4 banks** of CC mappings (`BANK_COUNT = 4`).
- ✅ **Shift layer**:
  - Momentary while shift is held.
  - Long press toggles latch mode (`LONG_PRESS_MS = 550`).
- ✅ Matrix-aware CC numbering with **serpentine row mapping**.
- ✅ Optional local LED indicators (bank/shift pins configurable in code).

---

## 🧭 Quick behavior map

- **Normal buttons (all except bank/shift):** Toggle state and send CC value `127` (ON) or `0` (OFF).
- **Bank button (`index 56`):** Cycles through banks `0 → 1 → 2 → 3 → 0`.
- **Shift button (`index 57`):**
  - Press/hold = momentary shift layer active.
  - Long press = toggle shift latch.

---

## 🧩 MIDI mapping logic

The firmware computes CC like this:

```text
cc = CC_BASE + matrixIndex + (activeBank * TOTAL_PINS)
if shift active: cc += LAYER_CC_OFFSET
cc = cc & 0x7F
```

Default constants:

- `MIDI_CHANNEL = 1`
- `CC_BASE = 16`
- `TOTAL_PINS = 64`
- `BANK_COUNT = 4`
- `LAYER_CC_OFFSET = 64`
- `CC_ON_VALUE = 127`
- `CC_OFF_VALUE = 0`

> Because CC is masked with `0x7F`, values wrap in the MIDI 0–127 range.

---

## 🗺️ Interactive pin map (0–63)

The logical matrix is 8×8:

- `row = index / 8`
- `col = index % 8`
- **Bank button:** row 7, col 0 (index 56)
- **Shift button:** row 7, col 1 (index 57)

<details>
<summary><strong>Click to expand matrix index map</strong></summary>

| Row \ Col | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| 1 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
| 2 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
| 3 | 24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 |
| 4 | 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 |
| 5 | 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 |
| 6 | 48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 |
| 7 | 56 (**BANK**) | 57 (**SHIFT**) | 58 | 59 | 60 | 61 | 62 | 63 |

</details>

### MCP23017 hardware pin distribution

Global pin indexing spans detected MCP chips:

- `chip = globalPin / 16`
- `localPin = globalPin % 16`

| Global pin range | MCP index | I2C address source | MCP local pins |
|---|---:|---|---|
| 0–15 | 0 | first detected (typically `0x20`) | 0–15 |
| 16–31 | 1 | next detected | 0–15 |
| 32–47 | 2 | next detected | 0–15 |
| 48–63 | 3 | next detected | 0–15 |

> The firmware can detect up to 8 chips, but the current input map uses 64 logical pins.

---

## 🔌 Wiring notes

- Arduino Mega I2C:
  - `SDA = pin 20`
  - `SCL = pin 21`
- MIDI output:
  - `TX1 (pin 18)` via `Serial1` at 31250 baud.
- Inputs are configured as `INPUT` + `pullUp(HIGH)` on MCP pins (active-low buttons).

---

## 🚀 Setup

1. Install required libraries in Arduino IDE:
   - `Adafruit MCP23017 / Adafruit_MCP23X17`
   - `Wire` (built-in)
2. Open `Arduino_Mega_MIDI_Controller_CentipedeShield.ino`.
3. Verify constants at the top (MIDI channel, CC base, bank count, etc.).
4. Upload to Arduino Mega.
5. Open Serial Monitor at **115200** to inspect startup scan/debug output.

---

## 🛠️ Customization checklist

- [ ] Change MIDI channel (`MIDI_CHANNEL`).
- [ ] Change first CC number (`CC_BASE`).
- [ ] Change bank count (`BANK_COUNT`).
- [ ] Change shift-layer offset (`LAYER_CC_OFFSET`).
- [ ] Move bank/shift button indices (`BANK_BUTTON_INDEX`, `SHIFT_BUTTON_INDEX`).
- [ ] Assign physical LED pins (`BANK_LED_PIN`, `SHIFT_LED_PIN`).
- [ ] Tune debounce/long-press timing.

---

## 📄 License

This project is licensed under the terms in [LICENSE](LICENSE).
