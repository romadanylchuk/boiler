# HKL-EA2 Board Reference (by HANKERILA)

## MCU
ESP32-WROOM-32N4

## GPIO Pin Map

| Function         | GPIO  | Notes                                  |
|------------------|-------|----------------------------------------|
| Digital IN1      | 36    | Active LOW (short to GND)              |
| Digital IN2      | 39    | Active LOW (short to GND)              |
| Temperature TEM1 | 33    | DS18B20 one-wire                       |
| Temperature TEM2 | 14    | DS18B20 one-wire                       |
| RS485_RX         | 35    |                                        |
| RS485_TX         | 32    |                                        |
| Relay01          | 2     | JQC-3FF-5VDC-1ZS, 10A/277VAC 12A/125VAC |
| Relay02          | 15    | JQC-3FF-5VDC-1ZS, 10A/277VAC 12A/125VAC |
| Buzzer           | 12    | Active HIGH                            |
| IIC SDA          | 4     | Shared by 12V and 5V IIC ports         |
| IIC SCL          | 16    | Shared by 12V and 5V IIC ports         |
| GSM_RX           | 13    | SIM7600 4G / SIM800L 2G                |
| GSM_TX           | 5     | SIM7600 4G / SIM800L 2G                |

## Ethernet (LAN8720 / HanRun HR911105A)
```
ETH_ADDR      = 0
ETH_POWER_PIN = -1
ETH_MDC       = 23
ETH_MDIO      = 18
ETH_TYPE      = ETH_PHY_LAN8720
ETH_CLK_MODE  = ETH_CLOCK_GPIO17_OUT
```

## Interfaces Summary

### 1. Digital Inputs (2x)
- Connector: IN1/IN2 on terminal strip
- Logic: LOW when shorted to GND (optocoupler isolated)

### 2. Temperature Sensors (2x DS18B20)
- TEM1 = GPIO33, TEM2 = GPIO14
- Connect: 3V3, GND, signal wire to TEM1 or TEM2

### 3. Relay Outputs (2x)
- Relay01 = GPIO2, Relay02 = GPIO15
- Each has NO / COM / NC contacts
- Rating: 10A 277VAC / 12A 125VAC

### 4. RS485
- RX = GPIO35, TX = GPIO32
- Supports ModBus-RTU (e.g., energy meters)

### 5. Buzzer
- GPIO12, used for alarms and status beeps

### 6. IIC Extension Port1 (12V power)
- SDA = GPIO4, SCL = GPIO16
- Expandable with DO8 module (+8 relays = 10 total)

### 7. IIC Extension Port2 (5V power)
- SDA = GPIO4, SCL = GPIO16
- Compatible with SHT3X temperature/humidity sensors

### 8. SIM7600 4G Module slot
- GSM_RX = GPIO13, GSM_TX = GPIO5
- Remote control, data upload to cloud

### 9. SIM800L 2G Module slot
- Same GPIO13/5 pins as 4G
- SMS notifications on alarm

### 10. Ethernet
- HanRun HR911105A (LAN8720)
- Stable wired LAN, supports MQTT over Ethernet

## Power
- Input: DC 12V (terminal block)
- Onboard: 5V and 3.3V rails available on terminal strip

## Programming
- USB Type-C
- Reset + Download buttons
- Arduino IDE compatible
- ESPHome compatible
- MQTT (WiFi + Ethernet)
