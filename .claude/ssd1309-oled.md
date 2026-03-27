# SSD1309 2.42" OLED Display Reference

## Specs
- Size: 2.42 inch
- Resolution: 128 x 64 pixels
- Color: Monochrome (White/Blue/Green/Yellow)
- Controller: SSD1309
- Interface: SPI (4-wire) or I2C
- Logic VDD: 1.65V – 3.3V
- Display VCC: 7V – 16V (internal boost converter)
- Current (normal, all ON): 17–25 mA
- Viewing angle: 160°

## Pin Definition (module connector)

| NO | Name | Function                                      |
|----|------|-----------------------------------------------|
| 1  | GND  | Ground                                        |
| 2  | VCC  | Power 3.3V (logic VDD)                        |
| 3  | SCL  | Clock (SPI SCLK / I2C SCL)                   |
| 4  | SDA  | Data (SPI MOSI / I2C SDA)                    |
| 5  | RES  | Reset (active LOW, pull HIGH for operation)   |
| 6  | DC   | Data/Command select (SPI only; HIGH=data)     |
| 7  | CS1  | Chip Select (active LOW; tie GND if unused)   |

> In I2C mode: DC and CS1 are not used. RES can be auto-reset via RC circuit.

## SSD1309 I2C Address
- Default: **0x3C** (SA0=0) or 0x3D (SA0=1)

## Initialization Sequence (SPI/I2C command sequence)
```
RES=0 → delay 1ms → RES=1 → delay 1ms
0xAE  // Display off
0x00  // Lower column start
0x10  // Higher column start
0x40  // Display start line
0x81, 0x32  // Contrast
0xA1  // Segment remap
0xA6  // Normal display (0xA7 = inverse)
0xA8, 0x3F  // Multiplex ratio 1/64
0xC8  // COM scan direction (remapped)
0xD3, 0x00  // Display offset
0xD5, 0xA0  // Clock divide / oscillator frequency
0xD9, 0xF1  // Pre-charge period
0xDA, 0x12  // COM pins hardware config
0xDB, 0x30  // VCOMH deselect level
0xAD, 0x8E  // Master configuration
0xAF  // Display ON
```

## Power-up Sequence
1. Power VDD
2. Send Display OFF (0xAE)
3. Initialize registers
4. Clear screen buffer
5. Power VCC (panel)
6. Delay 100ms
7. Send Display ON (0xAF)

## Power-down Sequence
1. Send Display OFF (0xAE)
2. Power down VCC
3. Delay 100ms (discharge)
4. Power down VDD

## Recommended Library (PlatformIO / Arduino)
- `adafruit/Adafruit SSD1306` — works with SSD1309 (compatible commands)
- `ThingPulse/ESP8266 and ESP32 OLED driver for SSD1306 displays` — lightweight option

## Connection to HKL-EA2 (I2C mode — RECOMMENDED)

| Display Pin | Connect To          | Notes                            |
|-------------|---------------------|----------------------------------|
| GND         | GND terminal        |                                  |
| VCC         | 3V3 terminal        | Logic power only (3.3V)          |
| SCL         | GPIO16 (IIC SCL)    | Shared I2C bus                   |
| SDA         | GPIO4  (IIC SDA)    | Shared I2C bus                   |
| RES         | 3V3 via 10K + 100nF cap to GND | Auto-reset on power-up |
| DC          | Not connected (I2C mode)       |                        |
| CS1         | GND                 | Tie LOW to enable                |

### Why I2C mode on HKL-EA2:
- The board exposes IIC Extension Port2 (5V) on GPIO4/GPIO16 — exact match
- 3.3V available on terminal strip for display logic
- No dedicated SPI header is exposed; SPI would require soldering free GPIOs
- SSD1309 I2C address 0x3C does NOT conflict with typical DO8 or SHT3X (0x44/0x45)
- Frees all control GPIOs — RES can be RC auto-reset, DC/CS not needed

### SPI mode (if I2C bus is congested):
Would need 5 free GPIOs (SCL, SDA, RES, DC, CS). No broken-out SPI header on HKL-EA2.
Possible but requires custom wiring to ESP32 pads.
