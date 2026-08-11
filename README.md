# HP LaserJet Front Panel Display Driver & RPG Demo (`hp-laserjet-screen-esp`)

An ESP8266 hardware driver and embedded game demo (*VFD Quest*) for dual front-panel display modules salvaged from HP Color LaserJet 1600/2600 series printers. Driven by dual **Rohm BU6740AK** display controllers over a shared SPI bus.

---

## 📷 Hardware Overview

| Front Panel (Dual Displays Running *VFD Quest*) | Rear Panel Wiring & PCB Interface |
| :---: | :---: |
| <img src="img/front.jpeg" alt="Front Panel VFD Quest Demo" width="400"/> | <img src="img/back.jpeg" alt="Rear PCB Hardware Wiring" width="400"/> |

---

## 🚀 Key Features

* **Dual Display Architecture**: Drives two stacked display panels simultaneously using hardware Chip Select (`CS1` / `CS2`) line multiplexing.
* **Full Controller Protocol Driver**: Low-level implementation of the 16-bit dual-nibble command/data framing required by the Rohm BU6740AK.
* **Front Panel Button Input Engine**: Integrated bidirectional SPI polling for reading the 4 active-low physical pushbuttons with 40ms software debouncing.
* **Status LED Control**: Independent driving of dual status indicator LEDs (Yellow & Green) per module.
* **Included RPG Demo (`game.cpp`)**: *VFD Quest*, a full turn-based retro RPG with state machine UI, battle mechanics, shop, inventory, and animated combat logs.

---

## 🔌 Pinout & Hardware Connections

### Display FPC (J701 Connector)
| Pin | Name | Type | Description |
| :---: | :--- | :---: | :--- |
| **1** | `MISO` | Output | Master In Slave Out (Button state feedback) |
| **2** | `3.3V` | Power | +3.3V DC Supply |
| **3** | `GND`  | Power | Common Ground |
| **4** | `MOSI` | Input  | Master Out Slave In (Command/Data framing) |
| **5** | `SCLK` | Input  | SPI Serial Clock |
| **6** | `/CS`  | Input  | Active-Low Chip Select |

### ESP8266 Interfacing Pin Map
| ESP8266 Pin | Function | Target Hardware |
| :--- | :--- | :--- |
| **`D1` (GPIO 5)** | Hardware `/CS1` | Top Display Module Chip Select |
| **`D2` (GPIO 4)** | Hardware `/CS2` | Bottom Display Module Chip Select |
| **`D5` (GPIO 14)**| Hardware `SCLK` | Shared SPI Serial Clock Line |
| **`D7` (GPIO 13)**| Hardware `MOSI` | Shared SPI MOSI Line |
| **`D6` (GPIO 12)**| Hardware `MISO` | Shared SPI MISO Line |

---

## ⚡ SPI Protocol & Initialization Sequence

### Bus Configuration
* **Bus Speed**: 250 kHz
* **Bit Order**: `MSBFIRST`
* **SPI Mode**: Mode 0 (`CPOL = 0`, `CPHA = 0`)
* **Transaction Delays**: ~15 µs between nibbles; ~45 µs after complete frame write.

### Transfer Framing Rules
Writing an 8-bit command or data byte requires sending two sequential 16-bit SPI transfers:

1. **Command Writes (`0x2700` Prefix Mask)**:
   * **Upper Nibble**: `0x2700 | (cmd & 0xF0)`
   * **Lower Nibble**: `0x2700 | ((cmd & 0x0F) << 4)`

2. **ASCII Data Writes (`0x6700` Prefix Mask)**:
   * **Upper Nibble**: `0x6700 | (data & 0xF0)`
   * **Lower Nibble**: `0x6700 | ((data & 0x0F) << 4)`

3. **Status LED Driving (`0x9000` Prefix Mask)**:
   * **Transfer**: `0x9000 | ((state & 0x03) << 4)`
   * **States**: `0` = Both Off, `1` = Yellow On, `2` = Green On, `3` = Both On.

4. **Button Polling**:
   * Send `0x0700` frame to trigger button state capture. Read 16-bit SPI response frame; extract lower 4 bits (`rx & 0x000F`).
   * **Logic**: Active-LOW (`0` = Pressed, `1` = Released).

### Rohm BU6740AK Initialization Routine
To initialize each module into 2-line mode prior to printing text, the driver sends the following 14-word hardware initialization sequence:

```cpp
uint16_t initSeq[] = {
  0x2730, 0x2730, 0x2730, 0x2720,
  0x2720, 0x2790, 0x2710, 0x2780,
  0x2750, 0x27e0, 0x2770, 0x2750,
  0x2760, 0x27c0
};
