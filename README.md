# 🎱 IoT Billiards Club Management System

<div align="center">

  <p><b>An automated, secure, and robust hardware-to-cloud management system for modern billiard clubs.</b></p>
  
  <p>
    <img src="https://img.shields.io/badge/Framework-ESP--IDF-orange?style=for-the-badge&logo=espressif" alt="ESP-IDF">
    <img src="https://img.shields.io/badge/Cloud-Cloudflare%20Workers-f38020?style=for-the-badge&logo=cloudflare" alt="Cloudflare Workers">
    <img src="https://img.shields.io/badge/Protocol-MQTT%20%2F%20WSS-0066cc?style=for-the-badge&logo=mqtt" alt="MQTT">
    <img src="https://img.shields.io/badge/Interface-Telegram%20Bot-2ca5e0?style=for-the-badge&logo=telegram" alt="Telegram">
    <img src="https://img.shields.io/badge/License-GPL%20v3-blue.svg?style=for-the-badge" alt="License">
  </p>
</div>

---

## 🌟 Overview

This project is a complete production-ready embedded and cloud solution designed to automate commercial billiard tables. It bridges the physical hardware (**ESP32**) with a serverless cloud backend (**Cloudflare Workers**) and a remote management interface (**Telegram Bot** via **EMQX MQTT broker**). 

The system handles real-time game timers, hardware relay switching for table illumination, secure QR-code/cashless payments, role-based access control, and offline session state recovery using an external RTC module.

---

## 🏗️ System Architecture & Data Flow

```text
 [ Client / Finik QR Pay ] 
            │ (Webhook)
            ▼
┌────────────────────────┐      WSS (MQTT)      ┌──────────────────┐
│  Cloudflare Worker     │ ───────────────────► │   EMQX Broker    │
│  (TypeScript / Bot)    │ ◄─────────────────── │  (Cloud Cluster) │
└────────────────────────┘                      └──────────────────┘
                                                         │
                                                         ▼
                                                ┌──────────────────┐
                                                │   ESP32 Device   │
                                                │  (ESP-IDF / C)   │
                                                └──────────────────┘

```

1. **Payments**: Users pay via a QR payment system (Finik), triggering a secure webhook to the **Cloudflare Worker**.
2. **Command Dispatch**: The Worker publishes an MQTT command (`PAYMENT_SUCCESS`, `RESET_SESSION`, etc.) over WebSockets (WSS) to the **EMQX Broker**.
3. **Hardware Execution**: The **ESP32** (running **ESP-IDF**) receives the command, activates the relay for the table lights, manages the countdown timer, and drives local displays.
4. **Offline Resilience**: In case of power outages or network disconnections, the onboard **DS3231 RTC** and **NVS flash** preserve session integrity and remaining time.

---

## 🛠️ Tech Stack

### Embedded Hardware & Firmware (`/main`)

* **Microcontroller:** ESP32 (Xtensa Dual-Core)
* **SDK / Framework:** [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/) (C language)
* **Peripherals & Drivers:**
* DS3231 Real-Time Clock (I2C) for offline time tracking
* 7-segment displays & TFT screens (SPI/GPIO) for local timers & animations
* KY-006 Active/Passive Buzzer for cyberpunk warning melodies (LEDC PWM)
* Hardware Relays for high-voltage light switching


* **Networking:** FreeRTOS tasks, MQTT client (ESP-MQTT over WSS), SNTP time synchronization, and embedded HTTP/Web server.

### Cloud Backend (`/worker`)

* **Runtime:** Cloudflare Workers (TypeScript)
* **Broker:** EMQX Cloud (MQTT over WebSockets)
* **Security & Auth:** Role-Based Access Control (`ADMIN` / `OPERATOR`) secured via Cloudflare KV and environment secrets.
* **Integrations:** Telegram Bot API (`/sendMessage`) and payment system webhooks.

---

## 📂 Project Structure

```text
├── main/
│   ├── Business_Logic/       # System state management, command dispatcher
│   ├── Networking_Services/  # MQTT client, Wi-Fi manager, Telegram bot interface, Web server
│   ├── Peripheral/           # DS3231 RTC, 7-segment & TFT displays, Buzzer (KY-006), Encoders
│   ├── UI/                   # Menu renderer, custom fonts
│   └── main.c                # Entry point
├── web/                      # Embedded HTML assets
├── partitions.csv            # Custom flash partition table
└── CMakeLists.txt            # ESP-IDF component registry & build config

```

---

## 🚀 Getting Started (Firmware Build)

Ensure you have the [ESP-IDF v6.1 SDK](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) installed on your machine.

1. **Clone the repository:**
```bash
git clone [https://github.com/your-username/billiards-controller.git](https://github.com/your-username/billiards-controller.git)
cd billiards-controller

```


2. **Configure the project target:**
```bash
idf.py set-target esp32

```


3. **Build the firmware:**
```bash
idf.py build

```


4. **Flash to your ESP32 device and monitor logs:**
```bash
idf.py -p PORT flash monitor

```


*(Replace `PORT` with your device port, e.g., `/dev/ttyUSB0` or `COM3`)*

---

## 📄 License

This project is open-source and available under the [GNU GPLv3 License](https://www.google.com/search?q=LICENSE).
