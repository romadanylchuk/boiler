# PCF8574 I2C GPIO Expander — Button Input Module

## Module
PCF8574 I/O Expansion Module (HONGWEIWEI or equivalent)

## I2C Address
- Default (A0=A1=A2=0): **0x20**
- Configurable 0x20–0x27 via A0/A1/A2 jumpers
- No conflict with SSD1309 display (0x3C)

## Connection to HKL-EA2

| PCF8574 Pin | Connect To        | Notes                          |
|-------------|-------------------|--------------------------------|
| VCC         | 3.3V terminal     | Not the 12V/5V IIC ports — 5V pull-ups on SDA/SCL are out of spec for ESP32 |
| GND         | GND terminal      |                                |
| SDA         | GPIO4             | Shared I2C bus with display    |
| SCL         | GPIO16            | Shared I2C bus with display    |
| INT         | not connected     | Firmware polls instead         |
| P0          | UP button → GND   |                                |
| P1          | DOWN button → GND |                                |
| P2          | ENTER button → GND |                               |
| P3          | BACK button → GND |                                |
| P4          | SETTINGS button → GND |                            |
| P5–P7       | Spare             |                                |

## Button Wiring
- PCF8574 has internal weak pull-ups on all I/O pins
- Button connects pin to GND — pin reads LOW when pressed, HIGH when released
- No external resistors needed

## Reading (polling, no INT)
- `ButtonReader::begin()` writes `0xFF` once — enables weak pull-ups (quasi-bidirectional inputs)
- `ButtonReader::update()` reads 1 byte via `Wire` every 20 ms (~0.2 ms bus time at 100 kHz)
- Debounce: raw P0–P4 state must be unchanged for 40 ms before it is accepted
- Press = 1→0 transition of the debounced state; release bounce cannot produce a second press
- OLED and buttons are both driven from `loop()`, so there is no I2C contention
- GPIO34 is free

## I2C Bus Summary (GPIO4 / GPIO16)
| Address | Device         |
|---------|----------------|
| 0x3C    | SSD1309 OLED   |
| 0x20    | PCF8574 buttons|

## PlatformIO Library
None — plain `Wire` (the chip has no registers, just one read/write port byte)
