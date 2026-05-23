<h1 align="center">🔌 STM32 UART IAP Bootloader + ESP32 Web OTA</h1>

<p align="center">
  <b>Wireless STM32 firmware updates over WiFi — no DFU, no ST-Link, no boot pins.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F446RE-03234B?style=for-the-badge&logo=stmicroelectronics&logoColor=white" alt="STM32F446RE">
  <img src="https://img.shields.io/badge/Bridge-ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white" alt="ESP32">
  <img src="https://img.shields.io/badge/Interface-UART-4B8BBE?style=for-the-badge" alt="UART">
  <img src="https://img.shields.io/badge/Update-Web%20OTA-1d9e75?style=for-the-badge" alt="Web OTA">
  <img src="https://img.shields.io/badge/HAL-STM32%20HAL-005C99?style=for-the-badge" alt="STM32 HAL">
</p>

---

A custom UART-based **In-Application Programming (IAP)** bootloader for the **STM32F446RE** that enables wireless firmware updates using an **ESP32 (NodeMCU ESP32S)** over WiFi.

The ESP32 hosts a web interface where you upload an STM32 firmware (`.bin`) file. It then streams the firmware over UART, and the STM32 bootloader programs internal flash memory and launches the updated application automatically.

---

> [!IMPORTANT]
> ## ⭐ No DFU · No ST-Link · No Boot Pins
> **This is the whole principle of the project.** The update path uses a fully custom application-level bootloader running from regular flash. It does **not** rely on the STM32's built-in DFU / system ROM bootloader, the embedded UART/USB DFU protocol, or any debug probe.
>
> | ❌ Not needed | ✅ What it uses instead |
> |---|---|
> | 🔧 BOOT0 pin toggling / download mode | 🚀 Custom bootloader in user flash |
> | 🔌 ST-Link / J-Link / debug probe | 📡 Plain UART link to an ESP32 |
> | 💾 DfuSe / dfu-util / DFU drivers | 🌐 A browser over WiFi |
>
> The device reprograms **itself** over a plain UART link with its own simple ACK/NACK protocol — handshake, flash erase, programming, and the jump to the new app are all implemented in user code in this repo.

---

## ✨ Features

- 📡 **Wireless firmware update via WiFi**
- 🚫 **No DFU / ST-Link / debug probe** — fully self-contained custom bootloader
- 🌐 Browser-based upload interface
- 🔁 UART firmware transfer (ESP32 → STM32)
- 🧹 Flash erase and programming support
- 🚀 Automatic application jump after update
- ✅ Application validity checking
- ⚡ Sector-wise erase optimization
- 📦 Chunk-based transfer (256 bytes)
- ⏱️ Bootloader timeout mode
- 💡 LED status indication

---

## 🏗️ System Architecture

```
        WiFi Network
             │
             ▼
 ┌───────────────────────┐
 │ Browser Interface     │
 │ Upload firmware.bin   │
 └──────────┬────────────┘
            │ HTTP
            ▼
 ┌───────────────────────┐
 │ ESP32 Web Server      │
 │ LittleFS Storage      │
 │ UART Transfer Engine  │
 └──────────┬────────────┘
            │ UART
            ▼
 ┌───────────────────────┐
 │ STM32F446RE           │
 │ UART IAP Bootloader   │
 │ Flash Programming     │
 └──────────┬────────────┘
            │
            ▼
      User Application
```

---

## 🔧 Hardware Used

**Controller Side**

- 🎛️ STM32F446RE
- 📶 NodeMCU ESP32S
- 🔌 USB-UART converter (optional)
- 💡 LEDs for status indication

**🔗 UART Connection**

| ESP32 | STM32F446RE |
|---|---|
| TX (GPIO17) | USART1 RX |
| RX (GPIO16) | USART1 TX |
| GND | GND |

---

## 🗺️ Memory Layout

STM32 Flash Size: **512 KB**

```
0x08000000 ┌───────────────────────┐
           │ Bootloader            │
           │ Sector 0-1            │
           │ (32 KB reserved)      │
0x08008000 ├───────────────────────┤
           │ User Application      │
           │ Starts here           │
           │ Sector 2 onward       │
0x08080000 └───────────────────────┘
```

Application start address:

```c
#define APP_ADDRESS 0x08008000
```

---

## ⚙️ Bootloader Operation

### 🔄 Boot Sequence

```
Power ON
   ↓
Bootloader starts
   ↓
Waits for handshake (0x7F)
   ↓
Firmware available?
   ├─ YES → Receive + Program Flash
   └─ NO  → Jump to application
```

### 🤝 Firmware Update Protocol

**Handshake**

ESP32 sends:

```
0x7F
```

STM32 responds:

```
ACK  = 0x79
NACK = 0x1F
```

**📤 Transfer Format**

```
[Handshake]   0x7F
     ↓
[File Size]   4 bytes
     ↓
Firmware chunks   256 bytes each
     ↓
ACK after every block
```

---

## 🧠 Flash Programming Flow

**1️⃣ Receive firmware size**

```c
uint32_t size
```

**2️⃣ Erase required sectors only** — dynamic erase:

```c
flash_erase_app(size);
```

**3️⃣ Receive chunks** — chunk size:

```c
#define CHUNK_SIZE 256
```

**4️⃣ Program Flash**

```c
HAL_FLASH_Program()
```

**5️⃣ Jump to new application**

```c
jump_to_app();
```

---

## 💡 Bootloader Status Indication

- 🟢 During boot window: **LED blinking** → bootloader active
- 🚀 Update completed: automatic application launch
- 🔴 No application found: fast blinking error state

---

## 🛠️ STM32 Bootloader Features Implemented

**🧹 Flash erase** — sector-based erase optimization:

```c
HAL_FLASHEx_Erase()
```

**✍️ Flash write** — word-aligned writes:

```c
HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD);
```

**✅ Application validation** — checks stack pointer validity via `app_is_valid()`, verifying the SRAM range:

```
0x20000000  to  0x20020000
```

---

## 📶 ESP32 Responsibilities

- 🌐 Hosts WiFi AP / network connection
- 📄 Serves firmware upload page
- 💾 Stores uploaded firmware in LittleFS
- 📖 Reads `.bin`
- 📏 Sends firmware size
- 📦 Transfers chunks
- ⏳ Waits for ACK
- 🔁 Continues transfer

---

## 🧰 Build Instructions

### 🎛️ STM32 Side

Open the project in **STM32CubeIDE**.

Build the project:

```
Project → Build
```

Generate the binary file:

```
Project → Properties
  → C/C++ Build
    → Settings
      → MCU Post Build Outputs
        → Enable binary file
```

Generated file:

```
application.bin
```

### 📶 ESP32 Side

Open `esp32_uart_ota_iap_working_uart.ino` using the **Arduino IDE** with the **ESP32 board package**, then upload to the **NodeMCU ESP32S**.

---

## 🚀 Upload Procedure

1. 🔋 Power STM32 + ESP32
2. 📶 Connect to WiFi
3. 🌐 Open browser: `http://ESP32_IP/`
4. 📤 Upload `application.bin`
5. 🔁 ESP32 transfers firmware
6. 🧠 STM32 programs flash
7. ✅ Application launches automatically

---

## 📁 Project Structure

```
project/
│
├── STM32_Bootloader/
│   └── main.c
│
├── ESP32_OTA/
│   └── esp32_uart_ota_iap_working_uart.ino
│
├── docs/
│   └── architecture.png
│
└── README.md
```

---

## 🔮 Suggested Improvements

- 🔐 CRC verification
- ⏯️ Resume interrupted update
- 🔒 Firmware encryption
- 🏷️ Version management
- ↩️ Rollback support
- 🛡️ Secure OTA authentication
- 📊 Progress bar UI
- 🪙 Dual-bank firmware update
- 🗜️ Compression support

---

## 🎬 Demo Workflow

```
Browser
   ↓
Upload BIN
   ↓
ESP32 Web Server
   ↓
UART Transfer
   ↓
STM32 Bootloader
   ↓
Flash Program
   ↓
Jump Application
```

---

## 🎯 Applications

- 🛰️ Remote firmware update systems
- 🏭 Industrial IoT devices
- 📡 Wireless embedded deployment
- 🌾 Smart agriculture devices
- 🌡️ Sensor nodes
- 🖥️ Edge computing systems
- 🤖 Robotics firmware upgrade
