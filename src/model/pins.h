#pragma once

// ─── DS18B20 Temperature Sensors ─────────────────────────────────────────────
#define PIN_DS18B20_FLOW     33   // TEM1 — flow temperature
#define PIN_DS18B20_RETURN   14   // TEM2 — return temperature

// ─── Relay Outputs ────────────────────────────────────────────────────────────
#define PIN_RELAY_HEATER      2   // Relay01 — electric heater contactor
#define PIN_RELAY_PUMP       15   // Relay02 — circulation pump

// ─── Buzzer ───────────────────────────────────────────────────────────────────
#define PIN_BUZZER           12   // Active HIGH

// ─── Digital Inputs ───────────────────────────────────────────────────────────
#define PIN_DIN_THERMOSTAT   36   // IN1 — external thermostat dry contact
// GPIO39 (IN2) — spare, reserved

// ─── I2C Bus (shared: OLED + PCF8574) ────────────────────────────────────────
#define PIN_I2C_SDA           4
#define PIN_I2C_SCL          16

// ─── I2C Device Addresses ────────────────────────────────────────────────────
#define I2C_ADDR_OLED       0x3C  // SSD1309 128x64 OLED
#define I2C_ADDR_PCF8574    0x20  // PCF8574 GPIO expander (A0=A1=A2=0)

// ─── PCF8574 Button Map (P0–P4) ──────────────────────────────────────────────
#define BTN_PIN_UP           0    // P0
#define BTN_PIN_DOWN         1    // P1
#define BTN_PIN_ENTER        2    // P2
#define BTN_PIN_BACK         3    // P3
#define BTN_PIN_SETTINGS     4    // P4
// P5–P7 spare

// ─── Ethernet LAN8720 (reserved, not active in WiFi mode) ────────────────────
#define ETH_MDC_PIN          23
#define ETH_MDIO_PIN         18
#define ETH_CLK_PIN          17
#define ETH_PHY_ADDR          0
