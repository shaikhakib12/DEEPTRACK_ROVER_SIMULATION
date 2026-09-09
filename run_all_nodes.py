#!/usr/bin/env python3
"""
=====================================================================
SIH 2026 MINE RESCUE ROVER - ADVANCED MESH ROUTING & CONTROLLER
=====================================================================
Dynamic Subterranean Mesh Topology with BFS Shortest-Route Optimization:
  ROVER ➔ (R1A / R1B) ➔ (R2 <-> R3) ➔ (R4A / R4B) ➔ GATEWAY
  + Dynamic Field Relays (R5, R6, ... added from Dashboard Map)

Features:
  - Full Individual ON / OFF Power Controls for ALL Nodes
  - Dynamic Relay Node Enrollment via MQTT (mine/mesh/add_relay)
  - Real-time Shortest-Path Calculation & Auto-Rerouting
  - MQTT Last Will and Testament (LWT) & Graceful Offline Shutdown
  - Interactive Hotkeys & Live Visual Terminal Status Dashboard
  - Bi-directional Synchronization with Web Dashboard via MQTT
=====================================================================
"""

import time
import json
import random
import threading
import sys
import atexit
import signal
from collections import deque
import paho.mqtt.client as mqtt

try:
    import winsound
    HAS_WINSOUND = True
except ImportError:
    HAS_WINSOUND = False

BROKER = "broker.emqx.io"
PORT = 1883

# Default Node Positions (matching Dashboard SVG coordinate space)
DEFAULT_COORDS = {
    "GATEWAY": {"x": 880, "y": 225},
    "R4A":     {"x": 730, "y": 140},
    "R4B":     {"x": 730, "y": 310},
    "R2":      {"x": 470, "y": 160},
    "R3":      {"x": 470, "y": 290},
    "R1A":     {"x": 240, "y": 110},
    "R1B":     {"x": 240, "y": 340},
    "ROVER":   {"x": 95,  "y": 225}
}

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

node_coords = dict(DEFAULT_COORDS)
ADJACENCY_GRAPH = {}

# Dynamic Hotkey Allocation for Nodes
AVAILABLE_HOTKEYS = ['9', '0', 'y', 'u', 'o', 'b', 'n', 'c', 'v', 'z', 'x']
node_key_map = {}

def assign_hotkeys():
    """Assigns hotkeys: 1-8 for core infrastructure, 9, 0, y, u... for dynamically added relays."""
    global node_key_map
    base_map = {
        '1': 'R1A',
        '2': 'R1B',
        '3': 'R2',
        '4': 'R3',
        '5': 'R4A',
        '6': 'R4B',
        '7': 'ROVER',
        '8': 'GATEWAY'
    }
    node_key_map = dict(base_map)
    extra_nodes = [n for n in node_status if n not in base_map.values()]
    for idx, r_name in enumerate(extra_nodes):
        if idx < len(AVAILABLE_HOTKEYS):
            node_key_map[AVAILABLE_HOTKEYS[idx]] = r_name

def rebuild_mesh_edges():
    """
    Reconstructs ADJACENCY_GRAPH dynamically based on node coordinates and radio range (285m),
    ensuring every node has bi-directional links to reachable neighbors.
    """
    global ADJACENCY_GRAPH
    RADIO_RANGE = 285.0
    graph = {n: [] for n in node_status}
    
    nodes = list(node_coords.keys())
    for i in range(len(nodes)):
        n1 = nodes[i]
        c1 = node_coords.get(n1, rover_pos if n1 == "ROVER" else {"x": 500, "y": 225})
        for j in range(i + 1, len(nodes)):
            n2 = nodes[j]
            c2 = node_coords.get(n2, rover_pos if n2 == "ROVER" else {"x": 500, "y": 225})
            dist = ((c1["x"] - c2["x"])**2 + (c1["y"] - c2["y"])**2)**0.5
            if dist <= RADIO_RANGE:
                if n2 not in graph[n1]: graph[n1].append(n2)
                if n1 not in graph[n2]: graph[n2].append(n1)

    # Ensure ROVER always has links to the 2 closest relays if it is slightly out of 285m range
    if "ROVER" in graph and len(graph["ROVER"]) == 0:
        c1 = rover_pos
        sorted_nodes = sorted(
            [n for n in nodes if n != "ROVER"],
            key=lambda n: ((c1["x"] - node_coords.get(n, {"x":500,"y":225})["x"])**2 + 
                          (c1["y"] - node_coords.get(n, {"x":500,"y":225})["y"])**2)
        )
        for n in sorted_nodes[:2]:
            graph["ROVER"].append(n)
            if "ROVER" not in graph[n]: graph[n].append("ROVER")

    ADJACENCY_GRAPH = graph

def register_new_relay(node_name, x, y):
    """Enrolls a newly placed relay node from the dashboard into the mesh network."""
    node_status[node_name] = True
    node_coords[node_name] = {"x": x, "y": y}
    assign_hotkeys()
    rebuild_mesh_edges()

def unregister_relay(node_name):
    """Decommissions a relay node removed by the dashboard user."""
    if node_name in node_status:
        del node_status[node_name]
    if node_name in node_coords:
        del node_coords[node_name]
    assign_hotkeys()
    rebuild_mesh_edges()

def reset_all_relays():
    """Resets mesh configuration back to standard 8-node blueprint."""
    global node_status, node_coords
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
    node_coords = dict(DEFAULT_COORDS)
    assign_hotkeys()
    rebuild_mesh_edges()

# Sensor Overrides
sensor_overrides = {
    "methane": False,
    "water": False,
    "person": False,
    "methane_val": None,
    "temp_val": None,
    "humidity_val": None
}

# Configurable Environment Sensor Thresholds
sensor_thresholds = {
    "methane_max": 25.0,     # Alert when CH4 >= 25% LEL
    "temp_max": 38.0,        # Alert when Temp >= 38°C
    "humidity_max": 85.0     # Alert when Humidity >= 85%
}

alarm_sound_enabled = True

def play_alarm_beep():
    """Plays an authoritative dual-tone siren buzzer in a background thread."""
    if not (alarm_sound_enabled and HAS_WINSOUND):
        return
    def _beep():
        try:
            winsound.Beep(1800, 180)
            winsound.Beep(1200, 180)
            winsound.Beep(2200, 240)
        except Exception:
            pass
    threading.Thread(target=_beep, daemon=True).start()

seq_number = 100
battery_pct = 92
running = True
rover_pos = {"x": 95, "y": 225}

# Initialize initial graph and hotkeys
assign_hotkeys()
rebuild_mesh_edges()

def drive_rover_terminal(dx, dy):
    global rover_pos
    rover_pos["x"] = max(50, min(950, rover_pos["x"] + dx))
    rover_pos["y"] = max(50, min(400, rover_pos["y"] + dy))
    node_coords["ROVER"] = {"x": rover_pos["x"], "y": rover_pos["y"]}
    rebuild_mesh_edges()
    if client and client.is_connected():
        client.publish("mine/rover/position", json.dumps({
            "x": rover_pos["x"],
            "y": rover_pos["y"],
            "senderId": "TERMINAL_ENGINE"
        }))
    print_status_box()

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

def find_shortest_path(start="ROVER", target="GATEWAY"):
    """
    Computes the shortest surviving route from start to target
    using Breadth-First Search (BFS), strictly ignoring OFFLINE nodes.
    """
    if not node_status.get(start, False) or not node_status.get(target, False):
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
    # Configure Last Will and Testament (LWT) so broker informs dashboard if terminal exits abruptly
    lwt_doc = json.dumps({
        "gateway": False,
        "nodes": {n: False for n in node_status},
        "senderId": "TERMINAL_ENGINE"
    })
    c.will_set("mine/gateway/node_health", lwt_doc, qos=1, retain=True)
    return c

client = create_mqtt_client(f"MineRouter_{random.randint(1000, 9999)}")

_is_shut_down = False
def shutdown_cleanup(*args):
    """Gracefully informs the web dashboard that all hardware simulation nodes are disconnected."""
    global running, _is_shut_down
    if _is_shut_down:
        return
    _is_shut_down = True
    running = False
    print(f"\n{RED}[TERMINAL SHUTDOWN]{RESET} Disconnecting master engine and broadcasting OFFLINE to dashboard...")
    try:
        shutdown_doc = json.dumps({
            "gateway": False,
            "nodes": {n: False for n in node_status},
            "senderId": "TERMINAL_ENGINE"
        })
        client.publish("mine/gateway/node_health", shutdown_doc, qos=1, retain=True)
        client.publish("mine/gateway/status", json.dumps({"online": False, "senderId": "TERMINAL_ENGINE"}), qos=1, retain=True)
        for n in node_status:
            client.publish(f"node/status/{n}", json.dumps({"node": n, "online": False, "senderId": "TERMINAL_ENGINE"}), qos=1, retain=True)
        time.sleep(0.35)
        client.loop_stop()
        client.disconnect()
    except Exception:
        pass
    print(f"{GREEN}[TERMINAL SHUTDOWN]{RESET} Offline status published. Master simulation stopped.\n")

atexit.register(shutdown_cleanup)
try:
    signal.signal(signal.SIGINT, lambda sig, frame: sys.exit(0))
    signal.signal(signal.SIGTERM, lambda sig, frame: sys.exit(0))
except Exception:
    pass

def broadcast_health_to_mqtt():
    """Publishes current node health map to the laptop dashboard."""
    health_payload = {
        "gateway": node_status.get("GATEWAY", False),
        "nodes": {n: node_status[n] for n in node_status},
        "senderId": "TERMINAL_ENGINE"
    }
    client.publish("mine/gateway/node_health", json.dumps(health_payload), qos=1, retain=True)
    client.publish("mine/gateway/status", json.dumps({"online": bool(node_status.get("GATEWAY", False)), "senderId": "TERMINAL_ENGINE"}), qos=1, retain=True)

    # Also individual status topics for Wokwi compatibility
    for n, active in node_status.items():
        client.publish(f"node/status/{n}", json.dumps({"node": n, "online": active, "senderId": "TERMINAL_ENGINE"}), qos=1, retain=True)

def on_connect(c, userdata, flags, rc, properties=None):
    client.subscribe([
        ("mine/dashboard/toggle_node", 0),
        ("mine/dashboard/toggle_sensor", 0),
        ("mine/dashboard/set_sensors", 0),
        ("mine/rover/position", 0),
        ("mine/rover/move", 0),
        ("mine/mesh/add_relay", 0),
        ("mine/mesh/delete_relay", 0),
        ("mine/mesh/reset_relays", 0)
    ])
    print(f"\n{GREEN}[NETWORK ENGINE]{RESET} Connected to MQTT Broker '{BROKER}:{PORT}'")
    client.publish("mine/gateway/status", json.dumps({"online": True, "senderId": "TERMINAL_ENGINE"}), qos=1, retain=True)
    broadcast_health_to_mqtt()

def on_message(c, userdata, msg):
    """Listens for remote commands sent from the Laptop Dashboard."""
    try:
        topic = msg.topic
        payload = json.loads(msg.payload.decode('utf-8'))
        if payload.get("senderId") == "TERMINAL_ENGINE":
            return

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
                print_status_box()

        elif topic == "mine/dashboard/set_sensors":
            if "methane" in payload:
                sensor_overrides["methane_val"] = float(payload["methane"])
            if "temperature" in payload:
                sensor_overrides["temp_val"] = float(payload["temperature"])
            if "humidity" in payload:
                sensor_overrides["humidity_val"] = float(payload["humidity"])
            if "water" in payload:
                sensor_overrides["water"] = bool(payload["water"])
            if "person" in payload:
                sensor_overrides["person"] = bool(payload["person"])
            if "methane_max" in payload:
                sensor_thresholds["methane_max"] = float(payload["methane_max"])
            if "temp_max" in payload:
                sensor_thresholds["temp_max"] = float(payload["temp_max"])
            print_status_box()

        elif topic == "mine/rover/position":
            if "x" in payload and "y" in payload:
                rover_pos["x"] = int(payload["x"])
                rover_pos["y"] = int(payload["y"])
                node_coords["ROVER"] = {"x": rover_pos["x"], "y": rover_pos["y"]}
                rebuild_mesh_edges()
                print_status_box()

        elif topic == "mine/rover/move":
            dx = int(payload.get("dx", 0))
            dy = int(payload.get("dy", 0))
            rover_pos["x"] = max(50, min(950, rover_pos["x"] + dx))
            rover_pos["y"] = max(50, min(400, rover_pos["y"] + dy))
            node_coords["ROVER"] = {"x": rover_pos["x"], "y": rover_pos["y"]}
            rebuild_mesh_edges()
            print_status_box()

        elif topic == "mine/mesh/add_relay":
            node_name = payload.get("node")
            nx = int(payload.get("x", 450))
            ny = int(payload.get("y", 225))
            if node_name:
                register_new_relay(node_name, nx, ny)
                print(f"\n{MAGENTA}[MESH EXPANSION]{RESET} Field Relay '{node_name}' deployed at ({nx}m, {ny}m)")
                broadcast_health_to_mqtt()
                print_status_box()

        elif topic == "mine/mesh/delete_relay":
            node_name = payload.get("node")
            if node_name and node_name in node_status:
                unregister_relay(node_name)
                print(f"\n{RED}[MESH DECOMMISSION]{RESET} Field Relay '{node_name}' removed from mesh")
                broadcast_health_to_mqtt()
                print_status_box()

        elif topic == "mine/mesh/reset_relays":
            reset_all_relays()
            print(f"\n{CYAN}[MESH BLUEPRINT]{RESET} Reset mesh to default 8 nodes")
            broadcast_health_to_mqtt()
            print_status_box()

    except Exception as e:
        pass

client.on_connect = on_connect
client.on_message = on_message

def print_status_box():
    """Renders a sleek real-time ASCII status box in the terminal."""
    best_path = find_shortest_path("ROVER", "GATEWAY")

    # Clear terminal cleanly (works on Windows & Linux)
    print("\033[2J\033[H", end="")
    print("="*74)
    print(f"{BOLD}{CYAN}   SIH 2026 MINE RESCUE ROVER - ADVANCED MESH ROUTING & CONTROLLER{RESET}")
    print("="*74)
    
    # 1. Shortest Path Display
    if best_path:
        path_str = f" {GREEN}➔{RESET} ".join([f"{BOLD}{n}{RESET}" for n in best_path])
        hops = len(best_path) - 1
        print(f"  {BOLD}OPTIMAL SHORTEST ROUTE:{RESET} [ {path_str} ]")
        print(f"  {BOLD}ROUTE METRICS:{RESET}          {CYAN}{hops} Hops{RESET} | Latency: ~{hops * 24}ms | Link Quality: {GREEN}EXCELLENT{RESET}")
    else:
        print(f"  {BOLD}OPTIMAL SHORTEST ROUTE:{RESET} {RED}⚠️ NO SURVIVING ROUTE REACHABLE TO GATEWAY (ISOLATED){RESET}")
        print(f"  {BOLD}ROUTE METRICS:{RESET}          {RED}PATH SEVERED - ALL REDUNDANT LINKS DOWN{RESET}")

    print("-" * 74)
    relay_count = len([n for n in node_status if n not in ("ROVER", "GATEWAY")])
    online_relays = len([n for n in node_status if n not in ("ROVER", "GATEWAY") and node_status[n]])
    print(f"  {BOLD}NODE POWER MATRIX (PRESS HOTKEY TO TOGGLE ON/OFF) [Relays: {online_relays}/{relay_count} Online]:{RESET}")

    # Build key mapping for display
    key_for_node = {v: k for k, v in node_key_map.items()}

    # Group nodes: Core 8 nodes first, then any extra relays added dynamically
    core_order = ["R1A", "R1B", "R2", "R3", "R4A", "R4B", "ROVER", "GATEWAY"]
    extra_order = [n for n in node_status if n not in core_order]
    display_nodes = [n for n in core_order if n in node_status] + sorted(extra_order)

    node_chips = []
    for n in display_nodes:
        st = f"{GREEN}ONLINE{RESET}" if node_status[n] else f"{RED}OFFLINE{RESET}"
        hk = key_for_node.get(n, "?")
        label = "ROV" if n == "ROVER" else ("GW" if n == "GATEWAY" else n)
        node_chips.append(f"  [{CYAN}{hk}{RESET}] {label:<4}: {st}")

    for i in range(0, len(node_chips), 2):
        c1 = node_chips[i]
        c2 = node_chips[i+1] if i+1 < len(node_chips) else ""
        print(f"{c1:<34} {c2:<34}")

    print("-" * 74)
    if sensor_overrides["methane_val"] is not None:
        ch4_str = f"{sensor_overrides['methane_val']:.1f} %LEL (SLIDER)"
        ch4_alert = sensor_overrides["methane_val"] >= sensor_thresholds["methane_max"]
    elif sensor_overrides["methane"]:
        ch4_str = "68.5 %LEL (FORCED)"
        ch4_alert = True
    else:
        ch4_str = "16.8 %LEL (AUTO)"
        ch4_alert = False

    gas_state = f"{RED}{ch4_str}{RESET}" if ch4_alert else f"{GREEN}{ch4_str}{RESET}"

    if sensor_overrides["temp_val"] is not None:
        temp_str = f"{sensor_overrides['temp_val']:.1f}°C"
        temp_alert = sensor_overrides["temp_val"] >= sensor_thresholds["temp_max"]
    else:
        temp_str = "29.2°C (AUTO)"
        temp_alert = False
    temp_state = f"{RED}{temp_str}{RESET}" if temp_alert else f"{CYAN}{temp_str}{RESET}"

    water_state = f"{RED}FLOOD DETECTED{RESET}" if sensor_overrides["water"] else f"{GREEN}NORMAL{RESET}"
    person_state = f"{YELLOW}POSSIBLE PERSON{RESET}" if sensor_overrides["person"] else f"{DIM}NO PERSON{RESET}"
    sound_state = f"{GREEN}ENABLED (SIREN ON){RESET}" if alarm_sound_enabled else f"{RED}MUTED{RESET}"

    print(f"  {BOLD}ROVER TELEOPERATION:{RESET} (X: {rover_pos['x']:3d}m, Y: {rover_pos['y']:3d}m) | Keys: {CYAN}↑/↓/←/→{RESET} or {CYAN}I/K/J/L{RESET} to Drive Rover via MQTT")
    print(f"  {BOLD}SENSOR STATUS:{RESET}       CH4: {gas_state} | Temp: {temp_state} | Water: {water_state} | AI: {person_state}")
    print(f"  {BOLD}SAFETY THRESHOLDS:{RESET}   CH4 Alert: {YELLOW}>={sensor_thresholds['methane_max']}% LEL{RESET} (Keys: {CYAN}[{RESET}/{CYAN}]{RESET}) | Temp Alert: {YELLOW}>={sensor_thresholds['temp_max']}°C{RESET} (Keys: {CYAN}<{RESET}/{CYAN}>{RESET})")
    print(f"  {BOLD}AUDIO ALARM SYSTEM:{RESET}  {sound_state}")
    print("="*74)
    print(f"  Hotkeys: {CYAN}Arrows/IJKL{RESET}=Drive Rover | {CYAN}1-8{RESET}=Core Nodes | {CYAN}9/0/y/u{RESET}=Extra Relays")
    print(f"           {YELLOW}m{RESET}=CH4 | {BLUE}w{RESET}=Water | {MAGENTA}p{RESET}=AI | {CYAN}[{RESET}/{CYAN}]{RESET}=CH4 Limit | {CYAN}<{RESET}/{CYAN}>{RESET}=Temp Limit | {RED}a{RESET}=Siren | {GREEN}r{RESET}=Reset | {RED}q{RESET}=Quit")
    print("="*74 + "\n")

def telemetry_broadcast_loop():
    """Simulates real-time telemetry passing across the calculated shortest path."""
    global seq_number, battery_pct

    while running:
        time.sleep(2.5)
        if not node_status.get("ROVER", False):
            continue

        seq_number += 1

        # Sensor read logic (respects remote dashboard sliders or terminal hotkeys)
        if sensor_overrides["methane_val"] is not None:
            methane = round(sensor_overrides["methane_val"], 1)
        elif sensor_overrides["methane"]:
            methane = 68.5
        else:
            methane = round(16.5 + random.uniform(-1.2, 1.8), 1)

        methane_status = "ALERT" if methane >= sensor_thresholds["methane_max"] else "NORMAL"

        if sensor_overrides["temp_val"] is not None:
            temp = round(sensor_overrides["temp_val"], 1)
        else:
            temp = round(29.2 + random.uniform(-0.4, 0.8), 1)

        if sensor_overrides["humidity_val"] is not None:
            humidity = round(sensor_overrides["humidity_val"], 0)
        else:
            humidity = round(65.0 + random.uniform(-2, 2), 0)

        water = 1 if sensor_overrides["water"] else 0
        person = 1 if sensor_overrides["person"] else 0
        alert = 1 if (methane >= sensor_thresholds["methane_max"] or temp >= sensor_thresholds["temp_max"] or water == 1 or person == 1) else 0

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
                "hops": hops,
                "senderId": "TERMINAL_ENGINE",
                "thresholds": {
                    "methane_max": sensor_thresholds["methane_max"],
                    "temp_max": sensor_thresholds["temp_max"]
                }
            }

            json_str = json.dumps(packet)
            
            # Print hop propagation trace
            status_txt = f"{RED}ALARM{RESET}" if alert else f"{GREEN}OK{RESET}"
            print(f"  {CYAN}»{RESET} [{time.strftime('%H:%M:%S')}] Pkt #{seq_number:03d} | Route: {BOLD}{path_str}{RESET} ({hops} hops) | CH4: {methane:4.1f}% | Temp: {temp}°C | Status: {status_txt}")

            # Trigger sound alarm if hazardous condition active
            if alert == 1:
                play_alarm_beep()

            # Send directly to Gateway output for Dashboard
            if node_status.get("GATEWAY", False):
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
                        "senderId": "TERMINAL_ENGINE",
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
                    # Check for extended key (Arrow keys: Up, Down, Left, Right)
                    if ch in (b'\x00', b'\xe0'):
                        try:
                            ch2 = msvcrt.getch()
                            if ch2 == b'H':    # UP
                                drive_rover_terminal(0, -20)
                            elif ch2 == b'P':  # DOWN
                                drive_rover_terminal(0, 20)
                            elif ch2 == b'K':  # LEFT
                                drive_rover_terminal(-25, 0)
                            elif ch2 == b'M':  # RIGHT
                                drive_rover_terminal(25, 0)
                        except Exception:
                            pass
                        continue

                    try:
                        key = ch.decode('utf-8', errors='ignore').lower()
                    except Exception:
                        continue

                    # Rover Directional Drive Hotkeys (I=Up, K=Down, J=Left, L=Right)
                    if key == 'i':
                        drive_rover_terminal(0, -20)
                    elif key == 'k':
                        drive_rover_terminal(0, 20)
                    elif key == 'j':
                        drive_rover_terminal(-25, 0)
                    elif key == 'l':
                        drive_rover_terminal(25, 0)

                    elif key in node_key_map:
                        n = node_key_map[key]
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

                    elif key == '[':
                        sensor_thresholds["methane_max"] = max(10.0, round(sensor_thresholds["methane_max"] - 2.5, 1))
                        print_status_box()

                    elif key == ']':
                        sensor_thresholds["methane_max"] = min(60.0, round(sensor_thresholds["methane_max"] + 2.5, 1))
                        print_status_box()

                    elif key in ('<', ','):
                        sensor_thresholds["temp_max"] = max(25.0, round(sensor_thresholds["temp_max"] - 1.0, 1))
                        print_status_box()

                    elif key in ('>', '.'):
                        sensor_thresholds["temp_max"] = min(60.0, round(sensor_thresholds["temp_max"] + 1.0, 1))
                        print_status_box()

                    elif key == 'r':
                        reset_all_relays()
                        sensor_overrides["methane"] = False
                        sensor_overrides["water"] = False
                        sensor_overrides["person"] = False
                        sensor_overrides["methane_val"] = None
                        sensor_overrides["temp_val"] = None
                        sensor_overrides["humidity_val"] = None
                        sensor_thresholds["methane_max"] = 25.0
                        sensor_thresholds["temp_max"] = 38.0
                        broadcast_health_to_mqtt()
                        print_status_box()

                    elif key == 's':
                        print_status_box()

                    elif key == 'q':
                        running = False
                        break
                else:
                    time.sleep(0.05)
        else:
            while running:
                key = input().strip().lower()
                if key in ('i', 'up', 'u'):
                    drive_rover_terminal(0, -20)
                elif key in ('k', 'down', 'd_nav'):
                    drive_rover_terminal(0, 20)
                elif key in ('j', 'left'):
                    drive_rover_terminal(-25, 0)
                elif key in ('l', 'right'):
                    drive_rover_terminal(25, 0)
                elif key in node_key_map:
                    n = node_key_map[key]
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
                elif key == '[':
                    sensor_thresholds["methane_max"] = max(10.0, round(sensor_thresholds["methane_max"] - 2.5, 1))
                    print_status_box()
                elif key == ']':
                    sensor_thresholds["methane_max"] = min(60.0, round(sensor_thresholds["methane_max"] + 2.5, 1))
                    print_status_box()
                elif key in ('<', ','):
                    sensor_thresholds["temp_max"] = max(25.0, round(sensor_thresholds["temp_max"] - 1.0, 1))
                    print_status_box()
                elif key in ('>', '.'):
                    sensor_thresholds["temp_max"] = min(60.0, round(sensor_thresholds["temp_max"] + 1.0, 1))
                    print_status_box()
                elif key == 'r':
                    reset_all_relays()
                    sensor_overrides["methane"] = False
                    sensor_overrides["water"] = False
                    sensor_overrides["person"] = False
                    sensor_overrides["methane_val"] = None
                    sensor_overrides["temp_val"] = None
                    sensor_overrides["humidity_val"] = None
                    sensor_thresholds["methane_max"] = 25.0
                    sensor_thresholds["temp_max"] = 38.0
                    broadcast_health_to_mqtt()
                    print_status_box()
                elif key == 's':
                    print_status_box()
                elif key == 'q':
                    running = False
                    break
    except KeyboardInterrupt:
        running = False
    finally:
        shutdown_cleanup()

if __name__ == "__main__":
    main()
