# HP LaserJet 1600/2600 Display Module Driver (`hp-laserjet-screen-esp`)

An ESP-compatible driver specification and micro-library for interfacing with front-panel screen modules from HP Color LaserJet 1600 and 2600 series printers.

Driven by the **Rohm BU6740AK** display controller.

---

## 📌 Features

* **16-Bit SPI Protocol**: Low-overhead hardware interface operating in SPI Mode 0.
* **Dual-Nibble Packet Framing**: Helper encoding routines for command and ASCII character writes.
* **Status LED Control**: Integrated driving for front-panel dual status LEDs (Yellow & Green).
* **Hardware Button Polling**: Readback transaction handling for the 4-button front panel interface.

---

## 🔌 Pinout & Connector Reference

The display module connects via a 6-pin FPC flex cable at **J701**. Logic levels are standard **3.3V**.

| Pin | Name | Type | Description |
| :---: | :--- | :---: | :--- |
| **1** | `MISO` | Output | Master In Slave Out (Button state feedback) |
| **2** | `3.3V` | Power | +3.3V DC Main Power Supply |
| **3** | `GND`  | Power | Common Ground |
| **4** | `MOSI` | Input  | Master Out Slave In (Data / Command frame line) |
| **5** | `SCLK` | Input  | SPI Serial Clock |
| **6** | `/CS`  | Input  | Active-Low Chip Select |

---

## ⚡ SPI Bus & Timing Specifications

| Parameter | Value / Setting |
| :--- | :--- |
| **Display Controller** | Rohm BU6740AK |
| **Data Width** | 16-bit |
| **Clock Frequency** | `1.0 MHz` |
| **SPI Mode** | Mode 0 (`CPOL = 0`, `CPHA = 0`) |
| **Chip Select (`/CS`)** | Active **LOW** during transactions |
| **Inter-Nibble Delay** | `~9 µs` (Delay between 1st and 2nd transfers) |
| **Post-Command Delay** | `~37 µs` (Execution delay after full byte frame) |

---

## 📝 Framing & Bitmask Rules

Sending an 8-bit command or ASCII character requires **two 16-bit SPI transfers** to complete a single byte frame.

### 1. Command Transfers (`cmd`)
Commands use prefix mask `0x2700`:
* **Transfer 1 (Upper Nibble):** `0x2700 | ((cmd & 0xF0) << 4)`
* **Transfer 2 (Lower Nibble):** `0x2700 | ((cmd & 0x0F) << 8)`

### 2. Data / Character Transfers (`ch`)
ASCII character writes use prefix mask `0x6700`:
* **Transfer 1 (Upper Nibble):** `0x6700 | ((ch & 0xF0) << 4)`
* **Transfer 2 (Lower Nibble):** `0x6700 | ((ch & 0x0F) << 8)`

---

## 💡 Status LED Control

LEDs are driven using a **single 16-bit SPI transfer** with format `0x90x0`:

| Value (`x`) | LED State | Command Frame |
| :---: | :--- | :---: |
| `0` | Both LEDs OFF | `0x9000` |
| `1` | Yellow LED ON | `0x9010` |
| `2` | Green LED ON | `0x9020` |
| `3` | Both LEDs ON | `0x9030` |

---

## 🔘 Button Polling

To poll front-panel button states:
1. Send a single 16-bit word (`0x0700`) on `MOSI`.
2. Simultaneously read the 16-bit response frame from `MISO`.
3. Extract the lower 4 bits (`response & 0x000F`).

> **Logic:** Active LOW (`0` = Pressed, `1` = Released).

| Bit | Target Button | Active State |
| :---: | :--- | :---: |
| `Bit 0` | Button 1 | `0` |
| `Bit 1` | Button 2 | `0` |
| `Bit 2` | Button 3 | `0` |
| `Bit 3` | Button 4 | `0` |

---

## 💻 Driver Reference Implementation (C++)

```cpp
#include <SPI.h>

#define PIN_CS 5

void writeDualNibble(uint16_t prefixMask, uint8_t dataByte) {
    uint16_t t1 = prefixMask | ((dataByte & 0xF0) << 4);
    uint16_t t2 = prefixMask | ((dataByte & 0x0F) << 8);

    digitalWrite(PIN_CS, LOW);
    SPI.transfer16(t1);
    delayMicroseconds(9);
    SPI.transfer16(t2);
    digitalWrite(PIN_CS, HIGH);

    delayMicroseconds(37);
}

// Send command to Rohm BU6740AK
void sendCommand(uint8_t cmd) {
    writeDualNibble(0x2700, cmd);
}

// Write ASCII character to display
void sendChar(char ch) {
    writeDualNibble(0x6700, (uint8_t)ch);
}

// Set status LED state (0 = Off, 1 = Yellow, 2 = Green, 3 = Both)
void setLEDs(uint8_t state) {
    uint16_t frame = 0x9000 | ((state & 0x03) << 4);
    digitalWrite(PIN_CS, LOW);
    SPI.transfer16(frame);
    digitalWrite(PIN_CS, HIGH);
}

// Poll button state bitmask (Returns lower 4 bits)
uint8_t readButtons() {
    digitalWrite(PIN_CS, LOW);
    uint16_t response = SPI.transfer16(0x0700);
    digitalWrite(PIN_CS, HIGH);
    return (uint8_t)(response & 0x0F);
}
