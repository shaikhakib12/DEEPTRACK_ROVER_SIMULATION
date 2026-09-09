#!/usr/bin/env python3
"""
=====================================================================
SIH 2026 MINE RESCUE ROVER - ADVANCED 8-NODE MESH & SHORTEST PATH ROUTER
=====================================================================
Dynamic Mesh Topology with Dijkstra / BFS Shortest-Route Optimization:
  ROVER ➔ (R1A / R1B) ➔ (R2 <-> R3) ➔ (R4A / R4B) ➔ GATEWAY

Features:
  - Full Individual ON / OFF Power Controls for ALL 8 Nodes
  - Dynamic Shortest-Path Calculation & Real-Time Rerouting
  - Interactive Hotkeys & Live Visual Terminal Dashboard
  - Bi-directional Synchronization with Web Dashboard via MQTT
=====================================================================
"""

import time
import json
import random
import threading
import sys
from collections import deque
import paho.mqtt.client as mqtt

try:
    import winsound
    HAS_WINSOUND = True
except ImportError:
    HAS_WINSOUND = False

BROKER = "broker.emqx.io"
PORT = 1883

# Node Power States (True = ONLINE, False = OFFLINE)
node_status = {
    "ROVER": True,
    "R1A": True,
    "R1B": True,
    "R2": True,
    "R3": True,
    "R4A": True,
    "R4B": True,
    "GATEWAY": True
}

# Sensor Overrides
sensor_overrides = {
    "methane": False,
    "water": False,
    "person": False
}

alarm_sound_enabled = True

def play_alarm_beep():
    """Plays an authoritative dual-tone siren buzzer in a background thread."""
    if not (alarm_sound_enabled and HAS_WINSOUND):
        return
    def _beep():
        try:
            # Dual-tone emergency siren
            winsound.Beep(1800, 180)
            winsound.Beep(1200, 180)
            winsound.Beep(2200, 240)
        except Exception:
            pass
    threading.Thread(target=_beep, daemon=True).start()

seq_number = 100
battery_pct = 92
running = True

# --- ANSI Color Codes for Visual Terminal ---
RESET   = "\033[0m"
BOLD    = "\033[1m"
GREEN   = "\033[38;5;48m"
RED     = "\033[38;5;196m"
YELLOW  = "\033[38;5;220m"
CYAN    = "\033[38;5;51m"
MAGENTA = "\033[38;5;141m"
BLUE    = "\033[38;5;39m"
DIM     = "\033[2m"

# Full Redundant Mesh Topology Graph Definition (Neighbor list)
ADJACENCY_GRAPH = {
    "ROVER": ["R1A", "R1B", "R2", "R3"],
    "R1A": ["R2", "R3", "R1B"],
    "R1B": ["R3", "R2", "R1A"],
    "R2": ["R4A", "R4B", "R3", "R1A", "R1B"],
    "R3": ["R4B", "R4A", "R2", "R1B", "R1A"],
    "R4A": ["GATEWAY", "R4B", "R2", "R3"],
    "R4B": ["GATEWAY", "R4A", "R3", "R2"],
    "GATEWAY": []
}

def find_shortest_path(start="ROVER", target="GATEWAY"):
    """
    Computes the shortest surviving route from start to target
    using Breadth-First Search (BFS), strictly ignoring OFFLINE nodes.
    """
    if not node_status[start] or not node_status[target]:
        return None

    queue = deque([[start]])
    visited = set([start])

    while queue:
        path = queue.popleft()
        current = path[-1]

        if current == target:
            return path

        for neighbor in ADJACENCY_GRAPH.get(current, []):
            if node_status.get(neighbor, False) and neighbor not in visited:
                visited.add(neighbor)
                queue.append(path + [neighbor])

    return None  # No valid surviving route exists

def create_mqtt_client(client_id):
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    return c

client = create_mqtt_client(f"MineRouter_{random.randint(1000, 9999)}")

def broadcast_health_to_mqtt():
    """Publishes current node health map to the laptop dashboard."""
    health_payload = {
        "gateway": node_status["GATEWAY"],
        "nodes": {n: node_status[n] for n in node_status}
    }
    client.publish("mine/gateway/node_health", json.dumps(health_payload))

    # Also individual status topics for Wokwi compatibility
    for n, active in node_status.items():
        if active:
            client.publish(f"node/status/{n}", json.dumps({"node": n, "online": True}))

def on_connect(c, userdata, flags, rc, properties=None):
    client.subscribe([
        ("mine/dashboard/toggle_node", 0),
        ("mine/dashboard/toggle_sensor", 0)
    ])
    print(f"\n{GREEN}[NETWORK ENGINE]{RESET} Connected to MQTT Broker '{BROKER}:{PORT}'")
    broadcast_health_to_mqtt()

def on_message(c, userdata, msg):
    """Listens for remote toggle commands sent from the Laptop Dashboard."""
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode('utf-8'))

        if topic == "mine/dashboard/toggle_node":
            node = payload.get("node")
            if node in node_status:
                node_status[node] = not node_status[node]
                status_str = f"{GREEN}ONLINE{RESET}" if node_status[node] else f"{RED}OFFLINE{RESET}"
                print(f"\n{MAGENTA}[REMOTE CMD FROM DASHBOARD]{RESET} Toggled Node {BOLD}{node}{RESET} ➔ {status_str}")
                broadcast_health_to_mqtt()
                print_status_box()

        elif topic == "mine/dashboard/toggle_sensor":
            sensor = payload.get("sensor")
            if sensor in sensor_overrides:
                sensor_overrides[sensor] = not sensor_overrides[sensor]
                print(f"\n{YELLOW}[REMOTE CMD]{RESET} Toggled sensor: {sensor}")

    except Exception as e:
        pass

client.on_connect = on_connect
client.on_message = on_message

def print_status_box():
    """Renders a sleek real-time ASCII status box in the terminal."""
    best_path = find_shortest_path("ROVER", "GATEWAY")

    # Clear terminal cleanly (works on Windows & Linux)
    print("\033[2J\033[H", end="")
    print("="*70)
    print(f"{BOLD}{CYAN}   SIH 2026 MINE RESCUE ROVER - MESH ROUTING & NODE CONTROLLER{RESET}")
    print("="*70)
    
    # 1. Shortest Path Display
    if best_path:
        path_str = f" {GREEN}➔{RESET} ".join([f"{BOLD}{n}{RESET}" for n in best_path])
        hops = len(best_path) - 1
        print(f"  {BOLD}OPTIMAL SHORTEST ROUTE:{RESET} [ {path_str} ]")
        print(f"  {BOLD}ROUTE METRICS:{RESET}          {CYAN}{hops} Hops{RESET} | Latency: ~{hops * 24}ms | Link Quality: {GREEN}EXCELLENT{RESET}")
    else:
        print(f"  {BOLD}OPTIMAL SHORTEST ROUTE:{RESET} {RED}⚠️ NO SURVIVING ROUTE REACHABLE TO GATEWAY (ISOLATED){RESET}")
        print(f"  {BOLD}ROUTE METRICS:{RESET}          {RED}PATH SEVERED - ALL REDUNDANT LINKS DOWN{RESET}")

    print("-" * 70)
    print(f"  {BOLD}NODE POWER MATRIX (PRESS NUMBER KEY TO TOGGLE ON/OFF):{RESET}")

    row1 = f"  [1] R1A: {GREEN}ONLINE{RESET}" if node_status["R1A"] else f"  [1] R1A: {RED}OFFLINE{RESET}"
    row2 = f"  [2] R1B: {GREEN}ONLINE{RESET}" if node_status["R1B"] else f"  [2] R1B: {RED}OFFLINE{RESET}"
    row3 = f"  [3] R2:  {GREEN}ONLINE{RESET}" if node_status["R2"] else f"  [3] R2:  {RED}OFFLINE{RESET}"
    row4 = f"  [4] R3:  {GREEN}ONLINE{RESET}" if node_status["R3"] else f"  [4] R3:  {RED}OFFLINE{RESET}"

    row5 = f"  [5] R4A: {GREEN}ONLINE{RESET}" if node_status["R4A"] else f"  [5] R4A: {RED}OFFLINE{RESET}"
    row6 = f"  [6] R4B: {GREEN}ONLINE{RESET}" if node_status["R4B"] else f"  [6] R4B: {RED}OFFLINE{RESET}"
    row7 = f"  [7] ROV: {GREEN}ONLINE{RESET}" if node_status["ROVER"] else f"  [7] ROV: {RED}OFFLINE{RESET}"
    row8 = f"  [8] GW:  {GREEN}ONLINE{RESET}" if node_status["GATEWAY"] else f"  [8] GW:  {RED}OFFLINE{RESET}"

    print(f"{row1:<30} {row2:<30}")
    print(f"{row3:<30} {row4:<30}")
    print(f"{row5:<30} {row6:<30}")
    print(f"{row7:<30} {row8:<30}")

    print("-" * 70)
    gas_state = f"{RED}ALARM (68.5 %LEL){RESET}" if sensor_overrides["methane"] else f"{GREEN}SAFE (16.8 %LEL){RESET}"
    water_state = f"{RED}FLOOD DETECTED{RESET}" if sensor_overrides["water"] else f"{GREEN}NORMAL{RESET}"
    person_state = f"{YELLOW}POSSIBLE PERSON{RESET}" if sensor_overrides["person"] else f"{DIM}NO PERSON{RESET}"
    sound_state = f"{GREEN}ENABLED (SIREN ON){RESET}" if alarm_sound_enabled else f"{RED}MUTED{RESET}"

    print(f"  {BOLD}SENSOR SIMULATION:{RESET} Methane: {gas_state} | Water: {water_state} | AI: {person_state}")
    print(f"  {BOLD}AUDIO ALARM SYSTEM:{RESET} {sound_state}")
    print("="*70)
    print(f"  Hotkeys: {CYAN}1-8{RESET}=Toggle Node | {YELLOW}m{RESET}=Methane | {BLUE}w{RESET}=Water | {MAGENTA}p{RESET}=Person | {RED}a{RESET}=Sound Siren | {GREEN}r{RESET}=Restore All | {RED}q{RESET}=Quit")
    print("="*70 + "\n")

def telemetry_broadcast_loop():
    """Simulates real-time telemetry passing across the calculated shortest path."""
    global seq_number, battery_pct

    while running:
        time.sleep(2.5)
        if not node_status["ROVER"]:
            continue

        seq_number += 1

        # Sensor read logic
        if sensor_overrides["methane"]:
            methane = 68.5
            methane_status = "ALERT"
        else:
            methane = round(16.5 + random.uniform(-1.2, 1.8), 1)
            methane_status = "NORMAL"

        temp = round(29.2 + random.uniform(-0.4, 0.8), 1)
        humidity = round(65.0 + random.uniform(-2, 2), 0)
        water = 1 if sensor_overrides["water"] else 0
        person = 1 if sensor_overrides["person"] else 0
        alert = 1 if (methane >= 25.0 or water == 1 or person == 1) else 0

        # Calculate optimal shortest path at this exact millisecond
        active_path = find_shortest_path("ROVER", "GATEWAY")

        if active_path:
            path_str = ">".join(active_path)
            hops = len(active_path) - 1

            packet = {
                "node": "ROVER",
                "seq": seq_number,
                "timestamp": int(time.time() * 1000),
                "methane": methane,
                "methane_status": methane_status,
                "temperature": temp,
                "humidity": humidity,
                "water": water,
                "person": person,
                "battery": battery_pct,
                "alert": alert,
                "path": path_str,
                "hops": hops
            }

            json_str = json.dumps(packet)
            
            # Print hop propagation trace
            status_txt = f"{RED}ALARM{RESET}" if alert else f"{GREEN}OK{RESET}"
            print(f"  {CYAN}»{RESET} [{time.strftime('%H:%M:%S')}] Pkt #{seq_number:03d} | Route: {BOLD}{path_str}{RESET} ({hops} hops) | CH4: {methane:4.1f}% | Temp: {temp}°C | Status: {status_txt}")

            # Trigger sound alarm if hazardous condition active
            if alert == 1:
                play_alarm_beep()

            # Send directly to Gateway output for Dashboard
            if node_status["GATEWAY"]:
                client.publish("mine/gateway/data", json_str)
                if alert == 1:
                    alert_doc = {
                        "seq": seq_number,
                        "alert": 1,
                        "methane": methane,
                        "water": water,
                        "person": person,
                        "path": path_str,
                        "timestamp": int(time.time() * 1000),
                        "reason": ("METHANE DANGER " if methane >= 25 else "") + ("WATER FLOOD " if water == 1 else "") + ("PERSON DETECTED" if person == 1 else "")
                    }
                    client.publish("mine/gateway/alerts", json.dumps(alert_doc))
        else:
            print(f"  {RED}✖{RESET} [{time.strftime('%H:%M:%S')}] Pkt #{seq_number:03d} | {RED}ROVER UNREACHABLE - NETWORK ISOLATED! No surviving route to Gateway.{RESET}")

def heartbeat_broadcast_loop():
    while running:
        time.sleep(2.0)
        broadcast_health_to_mqtt()

def main():
    global running
    client.connect(BROKER, PORT, 60)
    client.loop_start()

    t_tel = threading.Thread(target=telemetry_broadcast_loop, daemon=True)
    t_hb  = threading.Thread(target=heartbeat_broadcast_loop, daemon=True)
    t_tel.start()
    t_hb.start()

    print_status_box()

    node_keys = {
        '1': 'R1A',
        '2': 'R1B',
        '3': 'R2',
        '4': 'R3',
        '5': 'R4A',
        '6': 'R4B',
        '7': 'ROVER',
        '8': 'GATEWAY'
    }

    try:
        import msvcrt
        has_msvcrt = True
    except ImportError:
        has_msvcrt = False

    try:
        if has_msvcrt:
            while running:
                # Poll for single keypress without waiting for Enter
                if msvcrt.kbhit():
                    ch = msvcrt.getch()
                    try:
                        key = ch.decode('utf-8', errors='ignore').lower()
                    except Exception:
                        continue

                    if key in node_keys:
                        n = node_keys[key]
                        node_status[n] = not node_status[n]
                        broadcast_health_to_mqtt()
                        print_status_box()

                    elif key == 'm':
                        sensor_overrides["methane"] = not sensor_overrides["methane"]
                        print_status_box()

                    elif key == 'w':
                        sensor_overrides["water"] = not sensor_overrides["water"]
                        print_status_box()

                    elif key == 'p':
                        sensor_overrides["person"] = not sensor_overrides["person"]
                        print_status_box()

                    elif key == 'a':
                        alarm_sound_enabled = not alarm_sound_enabled
                        if alarm_sound_enabled:
                            play_alarm_beep()
                        print_status_box()

                    elif key == 'r':
                        for k in node_status:
                            node_status[k] = True
                        sensor_overrides["methane"] = False
                        sensor_overrides["water"] = False
                        sensor_overrides["person"] = False
                        broadcast_health_to_mqtt()
                        print_status_box()

                    elif key == 's':
                        print_status_box()

                    elif key == 'q':
                        running = False
                        print("\nExiting master simulation...")
                        break
                else:
                    time.sleep(0.05)
        else:
            while running:
                key = input().strip().lower()
                if key in node_keys:
                    n = node_keys[key]
                    node_status[n] = not node_status[n]
                    broadcast_health_to_mqtt()
                    print_status_box()
                elif key == 'm':
                    sensor_overrides["methane"] = not sensor_overrides["methane"]
                    print_status_box()
                elif key == 'w':
                    sensor_overrides["water"] = not sensor_overrides["water"]
                    print_status_box()
                elif key == 'p':
                    sensor_overrides["person"] = not sensor_overrides["person"]
                    print_status_box()
                elif key == 'a':
                    alarm_sound_enabled = not alarm_sound_enabled
                    if alarm_sound_enabled:
                        play_alarm_beep()
                    print_status_box()
                elif key == 'r':
                    for k in node_status:
                        node_status[k] = True
                    sensor_overrides["methane"] = False
                    sensor_overrides["water"] = False
                    sensor_overrides["person"] = False
                    broadcast_health_to_mqtt()
                    print_status_box()
                elif key == 's':
                    print_status_box()
                elif key == 'q':
                    running = False
                    print("\nExiting master simulation...")
                    break
    except KeyboardInterrupt:
        running = False

    client.loop_stop()
    client.disconnect()
    print("Simulation stopped.")

if __name__ == "__main__":
    main()
