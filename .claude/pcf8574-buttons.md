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
| VCC         | 3.3V terminal     |                                |
| GND         | GND terminal      |                                |
| SDA         | GPIO4             | Shared I2C bus with display    |
| SCL         | GPIO16            | Shared I2C bus with display    |
| INT         | GPIO34            | Falling edge interrupt, input-only GPIO |
| P0          | Button 1 → GND   |                                |
| P1          | Button 2 → GND   |                                |
| P2          | Button 3 → GND   |                                |
| P3          | Button 4 → GND   |                                |
| P4          | Button 5 → GND   |                                |
| P5–P7       | Spare             |                                |

## Button Wiring
- PCF8574 has internal weak pull-ups on all I/O pins
- Button connects pin to GND — pin reads LOW when pressed, HIGH when released
- No external resistors needed

## INT Pin Behavior
- INT pulls LOW when any input pin changes state
- Connect to GPIO34 (ESP32 input-only, interrupt-capable)
- Configure as FALLING edge interrupt in firmware
- On interrupt: read full PCF8574 port byte to determine which button changed

## I2C Bus Summary (GPIO4 / GPIO16)
| Address | Device         |
|---------|----------------|
| 0x3C    | SSD1309 OLED   |
| 0x20    | PCF8574 buttons|

## PlatformIO Library
`xreef/PCF8574 library` — supports interrupt-driven input
