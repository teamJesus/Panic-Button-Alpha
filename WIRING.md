WIRING and Test Guide
=====================

Hardware in this test:
- 5 momentary push buttons (wired active-LOW to digital pins with internal pull-ups)
- 1 piezo buzzer
- 1 1602 LCD with I2C backpack (4 wires: VCC, GND, SDA, SCL)
- 1 RFM9x LoRa transceiver module (optional)

Pin mapping used by the test sketch (`src/main.cpp`)
- Buttons: `D3`, `D4`, `D5`, `D6`, `D7` (connected to one side of each button; other side to GND)
  - Buttons use `INPUT_PULLUP` in software; wiring: button -> GND and other leg -> Dx
- Buzzer: `D8` (PWM capable)
- LCD (I2C): `VCC` (5V on Nano), `GND`, `SDA` (A4 on Nano), `SCL` (A5 on Nano)
  - Default I2C address in sketch: `0x27`. Use `i2c_scanner` to find address if your backpack differs.
- LoRa (optional):
  - Module pins (from module): `VIN`, `GND`, `G0`, `SCK`, `MISO`, `MOSI`, `CS`, `RST`
  - Wire to Arduino Nano as follows:
    - `VIN` -> `3.3V` (do NOT connect VIN to 5V)
    - `GND` -> `GND`
    - `G0` (DIO0) -> `D2` (required if you want interrupt-driven RX)
    - `SCK` -> `D13`
    - `MISO` -> `D12`
    - `MOSI` -> `D11`
    - `CS` / `NSS` -> `D10` (`PIN_LORA_SS`)
    - `RST` -> `D9`  (`PIN_LORA_RST`)
  - The sketch uses `PIN_LORA_DIO0 = 2` and `PIN_LORA_RST = 9` by default.

Important electrical notes
- RFM9x modules are 3.3V devices. Do NOT connect their VCC to 5V. Use 3.3V supply.
- The RFM9x I/O (MOSI/MISO/SCK/CS/RST/DIO) are 3.3V logic. If your MCU runs at 5V (Nano), use level shifting for MOSI/CS/RST/DIO lines or use a 3.3V-tolerant module.
- The I2C LCD backpack usually works at 5V (and will translate SDA/SCL), so wiring it to 5V on a Nano is OK.

How the sketch behaves
- On boot the LCD displays a status line and whether LoRa initialization succeeded (if enabled).
- Pressing any button (active LOW) displays the button number on the second row and triggers a short beep.

Enabling/disabling LoRa in the sketch
- By default LoRa is enabled in the sketch. To disable it:
  1. Comment out `#define USE_LORA` near the top of `src/main.cpp`.
  2. Re-upload.
- All pin assignments can be changed by editing the `#define PIN_*` constants at the top of `src/main.cpp`:
  - `PIN_BUTTON_1` through `PIN_BUTTON_5` for the five buttons
  - `PIN_BUZZER` for the buzzer
  - `PIN_LORA_SS`, `PIN_LORA_RST`, `PIN_LORA_G0` for LoRa pins
  - `I2C_LCD_ADDR`, `LCD_COLS`, `LCD_ROWS` for LCD settings
  - `BAUD_RATE`, `DEBOUNCE_MS`, `BEEP_DURATION_MS`, `BEEP_FREQ_HZ`, `LORA_FREQ` for operational parameters

If you have flash-size or memory issues
- The Nano with ATmega168 is limited in flash and RAM. If the current build size is a concern:
  - Comment out `#define USE_LORA` to disable LoRa and test buttons, buzzer, and LCD first.
  - Alternatively remove the LoRa `lib_deps` entry from `platformio.ini`.

Build and upload (PlatformIO)
------------------------------
From the project root run (PlatformIO task or terminal):
```
pio run --target upload
```
Or use the VSCode PlatformIO "Upload" task/button.

Quick verification checklist
1. Power the Nano and ensure Serial Monitor opens at 9600 baud.
2. LCD shows startup message and either `LoRa: disabled` or `LoRa: OK/FAILED`.
3. Press each button — the corresponding digit (1..5) appears on LCD row 2 and you hear a beep.
4. If LoRa enabled and `LoRa: OK` appears, you can extend the sketch to send/receive test packets.

If you want, I can also:
- Add a small LoRa TX/RX demo to send a short packet when a button is pressed.
- Create an `i2c_scanner` sketch to verify the LCD I2C address.

---
File: `WIRING.md`
