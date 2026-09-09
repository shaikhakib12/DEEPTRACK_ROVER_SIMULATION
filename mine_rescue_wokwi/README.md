# SIH 2026 Mine Rescue Rover - Redundant Communication Mesh Simulation

## 1. Project Overview & Architecture

In underground coal and metal mines, catastrophic tunnel collapses, rockfalls, methane gas buildup, and flooding create high-risk environments where conventional RF signals (Wi-Fi, 2.4 GHz) suffer from severe attenuation, line-of-sight blockage, and absorption.

This project implements an **8-Node Redundant Mesh Communication Topology** designed for the **Smart India Hackathon (SIH) 2026**. Because physical Wokwi cannot simulate raw LoRa RF physics (Semtech SX1278 / Ra-02) or physical sensor chemistry, this project utilizes **ESP32 Wi-Fi + MQTT** as a 100% faithful **software simulation of the multi-hop LoRa packet forwarding layer**.

```text
                         [ ROVER ]
                             |
                    +--------+--------+
                    |                 |
                 [ R1A ]           [ R1B ]  (Redundant Entry Relays)
                    |                 |
                    |                 |
                 [ R2 ] <-----------> [ R3 ] (Cross-Linked Middle Relays)
                    |                 |
                    |                 |
                 [ R4A ]           [ R4B ]  (Redundant Exit Relays)
                    |                 |
                    +--------+--------+
                             |
                        [ GATEWAY ]
                             |
                        (USB Serial / WebSockets)
                             |
                    [ LAPTOP DASHBOARD ]
```

---

## 2. Directory Structure

```text
mine_rescue_wokwi/
├── rover/               # Sensing Node (Pots, Gas, Water, SOS, Dual TX)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r1a/                 # Entry Relay Node A (Forward to R2)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r1b/                 # Entry Relay Node B (Forward to R3)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r2/                  # Middle Relay Node (Left Branch + Cross-Link to R3)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r3/                  # Middle Relay Node (Right Branch + Cross-Link to R2)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r4a/                 # Exit Relay Node A (Forward to Gateway)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── r4b/                 # Exit Relay Node B (Forward to Gateway)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── gateway/             # Base Station Gateway (Deduplication, USB Serial, Telemetry Bridge)
│   ├── diagram.json
│   ├── sketch.ino
│   └── libraries.txt
├── dashboard/           # Single-Page Real-Time Dark Mode HTML5/JS Dashboard
│   └── index.html
└── README.md            # Complete Setup, Demonstration Guide & Judge Protocol
```

---

## 3. How to Set Up the 8 Wokwi Projects

1. Open your browser and go to [wokwi.com](https://wokwi.com).
2. Click **Start New Project** ➔ select **ESP32** (or **ESP32 DevKit V1 / NodeMCU**).
3. In the project tabs:
   - Paste the contents of `sketch.ino` into the code editor.
   - Click the **diagram.json** tab and replace everything with the node's `diagram.json`.
   - Click the **Library Manager** (libraries icon on the left) or the `libraries.txt` tab and add:
     ```text
     PubSubClient
     ArduinoJson
     ```
4. Save the project and give it a clear name (e.g., `SIH-ROVER`, `SIH-R1A`, `SIH-R1B`, `SIH-R2`, `SIH-R3`, `SIH-R4A`, `SIH-R4B`, `SIH-GATEWAY`).
5. Repeat this for all 8 node folders:
   - Node 1: `rover/`
   - Node 2: `r1a/`
   - Node 3: `r1b/`
   - Node 4: `r2/`
   - Node 5: `r3/`
   - Node 6: `r4a/`
   - Node 7: `r4b/`
   - Node 8: `gateway/`

---

## 4. MQTT Broker Configuration

Each node is pre-configured with standard public EMQX test broker settings:
- **Broker:** `broker.emqx.io`
- **MQTT Port:** `1883` (ESP32 TCP)
- **WebSocket Port:** `8083` (ws://) or `8084` (wss://) (Dashboard browser connection)
- **Wi-Fi SSID:** `Wokwi-GUEST` (Provided natively by the Wokwi emulator)
- **Wi-Fi Password:** `""` (empty)

### Topic Routing Matrix

| Link / Function | MQTT Topic | Source | Destination |
| :--- | :--- | :--- | :--- |
| Rover Branch A | `mine/rover/to/r1a` | ROVER | R1A |
| Rover Branch B | `mine/rover/to/r1b` | ROVER | R1B |
| Relay 1A to 2 | `mine/r1a/to/r2` | R1A | R2 |
| Relay 1B to 3 | `mine/r1b/to/r3` | R1B | R3 |
| Middle Cross-Link (L ➔ R) | `mine/r2/to/r3` | R2 | R3 |
| Middle Cross-Link (R ➔ L) | `mine/r3/to/r2` | R3 | R2 |
| Middle R2 to Exit 4A | `mine/r2/to/r4a` | R2 | R4A |
| Middle R3 to Exit 4B | `mine/r3/to/r4b` | R3 | R4B |
| Exit 4A to Gateway | `mine/r4a/to/gateway` | R4A | GATEWAY |
| Exit 4B to Gateway | `mine/r4b/to/gateway` | R4B | GATEWAY |
| Telemetry to Dashboard | `mine/gateway/data` | GATEWAY | Dashboard |
| Emergency Alerts | `mine/gateway/alerts` | GATEWAY | Dashboard |
| Network Node Health | `mine/gateway/node_health` | GATEWAY | Dashboard |
| Node Heartbeats | `node/status/<NODE>` | All Nodes | Gateway / Dashboard |

---

## 5. How to Run the Laptop Dashboard

1. Navigate to `mine_rescue_wokwi/dashboard/index.html`.
2. Double-click to open `index.html` in any modern web browser (Chrome, Edge, Firefox, Brave, Safari).
3. **Zero setup required:** No Node.js, npm, Python server, or backend is needed. It connects directly to `broker.emqx.io` via secure WebSockets (`wss://8084` or `ws://8083`).
4. Look at the top right header: the badge will show `Gateway Online` in glowing green once connected!

---

## 6. Exact Order to Start the Simulations

To ensure judges experience zero dropped initial packets and immediately see a clean network formation:

1. **Step 1: Open `dashboard/index.html`** in your laptop browser.
2. **Step 2: Start `gateway`** (click the green Play button in Wokwi).
   - Check Serial Monitor: Gateway will connect to Wi-Fi, subscribe to topics, and the Dashboard status indicator will turn green.
3. **Step 3: Start Exit Relays `r4a` and `r4b`**.
   - In Dashboard: R4A and R4B health pills turn `ONLINE`.
4. **Step 4: Start Middle Relays `r2` and `r3`**.
   - In Dashboard: R2 and R3 health pills turn `ONLINE`.
5. **Step 5: Start Entry Relays `r1a` and `r1b`**.
   - In Dashboard: R1A and R1B health pills turn `ONLINE`.
6. **Step 6: Start `rover`**.
   - Telemetry begins pulsing every 2 seconds.
   - The SVG Topology will illuminate the active path (e.g. `ROVER ➔ R1A ➔ R2 ➔ R4A ➔ GATEWAY`).

---

## 7. How to Test Sensors & Alarms

All nodes support real-time interaction via **Wokwi GUI controls** and **Serial Monitor commands (115200 baud)**:

### A. Methane Gas Emergency
- **GUI:** Rotate the top-left potentiometer on the Rover node to maximum (clockwise).
- **Serial:** In the Rover Wokwi terminal, type:
  ```text
  m
  ```
  *(Press Enter)*
- **Outcome:**
  - Rover immediately flashes its Red Alert LED (GPIO 4).
  - Gateway Serial Monitor displays: `Methane: 68.5 %LEL` | `Alert: EMERGENCY ACTIVE`.
  - Dashboard triggers red flashing banner, Methane badge turns `DANGER HIGH`, and alert is logged with an audible emergency UI state.

### B. Water Flooding Detection
- **GUI:** Click and hold the blue pushbutton (**Water Flood**) on the Rover node.
- **Serial:** Type `w` in the Rover terminal.
- **Outcome:**
  - Packet field `water: 1` sent immediately.
  - Gateway Serial displays: `Water: FLOOD DETECTED` | `Alert: EMERGENCY ACTIVE`.
  - Dashboard Water status badge flashes red `FLOOD DETECTED`.

### C. Thermal AI Person Detection
- **GUI:** Click the yellow pushbutton (**Person / SOS**) on the Rover node.
- **Serial:** Type `p` in the Rover terminal.
- **Outcome:**
  - Packet field `person: 1` transmitted.
  - Dashboard Thermal canvas immediately generates a 37°C humanoid heatmap signature.
  - Person badge turns `POSSIBLE PERSON (HOTSPOT)`.

### D. Sensor Failure / Disconnection ("NOT MEASURED" State)
- **Serial:** In the Rover terminal, type:
  ```text
  f
  ```
- **Outcome:**
  - Rover simulates open-circuit / sensor disconnection.
  - Telemetry sends `methane: -1` and `methane_status: "NOT MEASURED"`.
  - Gateway displays: `Methane: NOT MEASURED`.
  - Dashboard displays an amber `NOT MEASURED` indicator. **It never falsely shows 0 %LEL as "safe".**

### E. Return to Safe Normal State
- In the Rover terminal, type:
  ```text
  n
  ```
  All overrides clear, and telemetry returns to safe baseline values.

---

## 8. Complete Test Procedure for Demonstrating Redundancy to an SIH Judge

### Demo Objective
Prove to the judging panel that the communication link **never drops**, even under multiple physical node cutoffs, tunnel collapses, or relay battery failures.

### Step-by-Step Demonstration Protocol

#### Stage 1: Baseline Dual-Branch Redundancy
1. Show the Dashboard running normally.
2. Note the path: `ROVER > R1A > R2 > R4A > GATEWAY`.
3. Open the Gateway Serial Monitor: Show the judge how Gateway accepts the first arrival from Branch A, while logging:
   `[GATEWAY DEDUPLICATION] Packet #X via 'ROVER>R1B>R3>R4B' dropped (duplicate filtered)`.
4. **Judge takeaway:** Both branches are operational simultaneously, providing instant zero-latency backup.

#### Stage 2: Simulating Collapse of Entry Relay R1A
1. In the `r1a` Wokwi simulation window, click the red **Toggle Fault** pushbutton (or type `f` in its serial terminal).
2. The Red Fault LED on R1A turns ON; R1A stops forwarding packets.
3. Look at the Dashboard:
   - Within 5 seconds, the R1A status badge turns `OFFLINE`.
   - The path seamlessly switches to: `ROVER > R1B > R3 > R4B > GATEWAY`.
   - **Zero telemetry packets were dropped!** The Rover sequence numbers remain continuous.

#### Stage 3: Simulating Cross-Link Routing (Failure of Middle Relay R3)
1. Now, in `r3` simulation, press the red **Toggle Fault** button (or type `f`).
2. R3 is now dead.
3. Bring R1A back online (press its toggle button or type `f` in R1A terminal).
4. Now cut off Exit Relay R4A (type `f` in `r4a`).
5. **Observe the Cross-Link in action:**
   - Rover transmits to R1A.
   - R1A forwards to R2.
   - R2 forwards across the cross-link to R3 (or R3 to R2).
   - Packets reach Gateway via: `ROVER > R1A > R2 > R3 > R4B > GATEWAY`!
6. Show the judge that the loop prevention mechanism prevented packet loops, while bridging across the surviving middle relay!

#### Stage 4: Restoration
1. Toggle all nodes back to normal.
2. Show the judge the health table automatically repopulating all green `ONLINE` badges in the dashboard.

---

## 9. Hardware Mapping: Simulation vs. Physical System

| Component | Wokwi Simulation Layer | Real Hardware Production Implementation | Rationale |
| :--- | :--- | :--- | :--- |
| **Telemetry Network** | Wi-Fi + MQTT (JSON) | **Semtech SX1278 (Ra-02) 433 MHz LoRa** | LoRa provides penetrating subterranean range (up to 1.5 km in tunnels), but bandwidth is low (~5 kbps). Telemetry JSON is optimized under 250 bytes. |
| **Relay Mesh Logic** | Dedicated ESP32s | **ESP32 + Ra-02 LoRa Node Modules** | Exact same routing logic, sequence number deduplication, and path appending runs on the physical microcontrollers. |
| **Methane Sensor** | Potentiometer (0–60 %LEL) | **MQ-4 / Winsen MP-4 Catalytic Sensor** | Calibrated for 0–100% Lower Explosive Limit (LEL) with intrinsically safe zener barriers. |
| **Environmental** | Potentiometer | **DHT22 / BME280 (I2C)** | Calibrated ambient temperature, humidity, and atmospheric pressure. |
| **Thermal Sensing** | Canvas Heatmap Generator | **Melexis MLX90640 (32x24 IR Array)** | Connected via I2C to Rover ESP32; detects human body temperature signatures (36°C–38°C) even through dense smoke. |
| **Live Video** | Wi-Fi MJPEG Stream Panel | **ESP32-CAM (OV2640) over 2.4 GHz Wi-Fi** | LoRa cannot carry video frames. The camera creates a short-range Wi-Fi hotspot or connects via directional tether for optical video inspection. |
| **Base Station Bridge**| Gateway ESP32 MQTT Bridge | **ESP32 USB LoRa Receiver** | Plugs directly into rescue coordinator's ruggedized laptop via USB serial (COM port) or Ethernet. |

---

## 10. Why Video Uses Wi-Fi Instead of LoRa

A critical engineering question judges will ask: **"Why doesn't your rover send video over LoRa?"**

- **Bandwidth Limitations:** LoRa at SF7 / 125 kHz provides a data rate of approximately **5.4 kbps**.
- A single 320x240 compressed JPEG frame is approximately **6,000 to 10,000 bytes (48,000 to 80,000 bits)**.
- Transmitting just **one video frame** over LoRa would take **10 to 15 seconds**, completely jamming all life-critical gas alarms and telemetry packets!
- **Engineering Solution:**
  1. **Primary Life-Safety Channel (LoRa):** Continuous, ultra-reliable delivery of toxic gas levels, temperature, water flooding, and binary thermal human detection alerts.
  2. **Secondary High-Bandwidth Channel (ESP32-CAM Wi-Fi):** Activated on-demand for live optical inspection when the rover halts near a victim or blockage.

---

## 11. Troubleshooting & Tips

- **MQTT Disconnections:** If `broker.emqx.io` is experiencing high public traffic, you can switch the `MQTT_SERVER` in all `sketch.ino` files and `index.html` to `test.mosquitto.org` or `broker.hivemq.com`.
- **Wokwi Serial Speed:** Ensure all Wokwi serial monitors are set to **115200 baud**.
- **Browser WebSockets:** If opening `index.html` locally via `file://`, modern browsers allow `ws://` to `broker.emqx.io:8083`. If hosted on an HTTPS server (like GitHub Pages), `index.html` automatically selects `wss://broker.emqx.io:8084` to prevent mixed-content blocks.
