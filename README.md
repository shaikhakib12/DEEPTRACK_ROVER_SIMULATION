<div align="center">

# 🚜 DEEPTRACK ROVER - SMART MINE RESCUE SYSTEM
### SIH 2026 | 8-Node Redundant Mesh Communication & Dynamic Shortest-Path Optimizer

[![Vercel Deployment](https://img.shields.io/badge/Deployed%20on-Vercel-black?style=for-the-badge&logo=vercel)](https://vercel.com)
[![Platform](https://img.shields.io/badge/Platform-ESP32%20%7C%20FreeRTOS-red?style=for-the-badge&logo=espressif)](https://www.espressif.com)
[![Communication](https://img.shields.io/badge/Protocol-LoRa%20Mesh%20%2F%20MQTT-blue?style=for-the-badge&logo=mqtt)](https://mqtt.org)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

<p align="center">
  <strong>Fault-tolerant subterranean communication mesh designed for hazardous underground coal/metal mines.</strong><br>
  Features dynamic BFS rerouting, real-time telemetry streaming, interactive web mission control, and automated hazard alerts.
</p>

</div>

---

## 📌 Problem Statement & Context

Underground mining environments suffer from catastrophic tunnel collapses, rockfalls, methane gas pocket explosions, and flash flooding. Traditional RF signals (2.4 GHz Wi-Fi, cellular) suffer extreme attenuation and complete line-of-sight blockage underground.

**DEEPTRACK ROVER** solves this with an **8-Node Self-Healing Redundant Mesh Topology**:
- If any primary relay node gets buried or loses power, packets **instantly and automatically reroute** through alternate redundant relay paths without dropping telemetry.
- Telemetry streams to the surface base station gateway, visualizing gas concentrations, flooding levels, and survivor presence in real time.

---

## ⚡ System Architecture

```text
                             [ ROVER ]
                       (Sensing & Camera Unit)
                                 |
                 +---------------+---------------+
                 |                               |
              [ R1A ]                         [ R1B ]          (Redundant Entry Relays)
                 |                               |
                 |       CROSS-LINK FAILOVER     |
              [ R2 ] <=========================> [ R3 ]        (Middle Relays)
                 |                               |
                 |                               |
              [ R4A ]                         [ R4B ]          (Redundant Exit Relays)
                 |                               |
                 +---------------+---------------+
                                 |
                            [ GATEWAY ]
                      (Surface Base Station)
                                 |
                     (MQTT WebSockets / JSON)
                                 |
                     [ LAPTOP MISSION CONTROL ]
```

---

## ✨ Key Features

- 🛰️ **Dynamic Shortest-Path Routing Engine**:
  - Implements real-time Breadth-First Search (BFS) / Dijkstra pathfinding over active network nodes.
  - Automatically selects the path with the lowest hop count and minimum transmission latency.
- 🔄 **Self-Healing Failover**:
  - If `R1A` fails $\rightarrow$ traffic automatically routes through `R1B`.
  - If middle repeater `R2` fails $\rightarrow$ traffic bridges via the cross-link to `R3`.
- 📊 **Interactive Web Mission Control Dashboard**:
  - Real-time SVG mine shaft topology with animated laser pulses tracing the shortest route.
  - Interactive click-to-toggle power matrix for all 8 nodes.
  - Gas meters (Methane %LEL), Water Inundation warning, and Sparkline trend charts.
  - Thermal IR false-color heatmap with survivor hotspot detection.
- 🚨 **Dual Hardware & Web Audio Alarm System**:
  - **Terminal**: High-pitch dual-tone industrial hardware buzzer via `winsound`.
  - **Dashboard**: High-decibel industrial siren synthesized directly in-browser using the Web Audio API.
- 🎮 **Instant Terminal CLI Control**:
  - Non-blocking single-keypress controls (`1-8` node toggling, `m` gas spike, `w` flood, `p` person detect, `a` siren toggle).

---

## 🗂️ Project Structure

```text
DEEPTRACK_ROVER_SIMULATION/
├── index.html                   # Root Dashboard (Ready for instant 1-click Vercel deploy)
├── run_all_nodes.py             # Master Python Multi-Node Runner & Routing Simulator
├── START_ALL_NODES.bat          # One-click Windows batch launcher
├── platformio.ini               # Embedded PlatformIO project configuration
├── diagram.json                 # Master Wokwi hardware wiring definition
├── mine_rescue_wokwi/
│   ├── dashboard/               # Standalone Mission Control Dashboard UI
│   ├── rover/                   # Node 1: Sensing Rover (ESP32 + Gas/Water/Temp)
│   ├── r1a/                     # Node 2: Primary Entry Relay
│   ├── r1b/                     # Node 3: Redundant Entry Relay
│   ├── r2/                      # Node 4: Left Middle Relay (Cross-linked to R3)
│   ├── r3/                      # Node 5: Right Middle Relay (Cross-linked to R2)
│   ├── r4a/                     # Node 6: Primary Exit Relay
│   ├── r4b/                     # Node 7: Redundant Exit Relay
│   ├── gateway/                 # Node 8: Base Station Gateway (USB Serial / MQTT Bridge)
│   └── README.md                # Detailed technical protocol documentation
└── README.md                    # Project documentation
```

---

## 🚀 Quick Start Guide

### 1️⃣ Run the Terminal Simulation & Mesh Controller

Ensure you have Python 3 installed, then run:

```powershell
pip install paho-mqtt
python run_all_nodes.py
```
*(Or simply double-click `START_ALL_NODES.bat` on Windows)*

### 2️⃣ Open the Mission Control Dashboard

Simply double-click `index.html` in your browser, or deploy to **Vercel** with zero configuration!

---

## 🎮 Terminal Hotkey Controls

| Key | Action | Function |
| :---: | :--- | :--- |
| `1` | **Toggle R1A** | Powers OFF/ON primary entry repeater |
| `2` | **Toggle R1B** | Powers OFF/ON redundant entry repeater |
| `3` | **Toggle R2** | Powers OFF/ON middle repeater 2 (Diverts across cross-link to R3) |
| `4` | **Toggle R3** | Powers OFF/ON middle repeater 3 |
| `5` | **Toggle R4A** | Powers OFF/ON portal exit repeater 4A |
| `6` | **Toggle R4B** | Powers OFF/ON portal exit repeater 4B |
| `7` | **Toggle ROVER** | Powers OFF/ON Rover telemetry stream |
| `8` | **Toggle GATEWAY** | Powers OFF/ON Surface Base Station |
| `m` | **Methane Hazard** | Simulates dangerous gas spike ($68.5\%\ \text{LEL}$) $\rightarrow$ Triggers alarms |
| `w` | **Water Flood** | Simulates tunnel water inundation |
| `p` | **Survivor Detected**| Simulates AI thermal hotspot / Humanoid acquisition |
| `a` | **Audio Siren** | Toggles audible emergency siren ON / MUTED |
| `r` | **Restore All** | Restores all nodes to ONLINE and clears hazards |
| `q` | **Quit** | Shuts down simulation cleanly |

---

## 🌐 Live Web Deployment on Vercel

1. Push this repository to GitHub.
2. Go to [vercel.com](https://vercel.com) $\rightarrow$ **Add New Project**.
3. Select this repository: `DEEPTRACK_ROVER_SIMULATION`.
4. Leave **Root Directory** as `.` (root).
5. Click **Deploy**! 🚀

---

## 🛠️ Technology Stack

- **Firmware / Embedded**: ESP32 DevKit V1, FreeRTOS, Arduino C++
- **Simulation**: Wokwi Embedded Simulator
- **Protocols**: LoRa SX1278 (Simulated over MQTT PubSubClient / WebSockets)
- **Algorithms**: BFS / Dijkstra Dynamic Shortest Path Optimization
- **Frontend**: HTML5, SVG Dynamic Animations, Web Audio API, Canvas 2D
- **Host Controller**: Python 3 (Threading, Windows `msvcrt`, `winsound`)

---

<div align="center">
  <sub>Developed for Smart India Hackathon (SIH) 2026 - Mine Rescue Category</sub>
</div>