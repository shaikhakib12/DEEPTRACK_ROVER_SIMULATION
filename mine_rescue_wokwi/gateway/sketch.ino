/*
 * =====================================================================
 * SIH 2026 MINE RESCUE ROVER - BASE STATION GATEWAY NODE
 * =====================================================================
 * Microcontroller : ESP32 DevKit V1 (Wokwi Simulation)
 * Role            : Redundant Gateway Interface & USB Serial / Dashboard Bridge
 *
 * Subscribes to:
 *   - Topic 1: mine/r4a/to/gateway (Exit Relay 4A)
 *   - Topic 2: mine/r4b/to/gateway (Exit Relay 4B)
 *   - Topic 3: node/status/+       (Heartbeats from all network nodes)
 * Publishes:
 *   - mine/gateway/data        (Clean, deduplicated telemetry for dashboard)
 *   - mine/gateway/alerts      (Emergency alerts on gas / flood / person)
 *   - mine/gateway/node_health (Network node heartbeat health summary)
 *   - node/status/GATEWAY      (Gateway heartbeat)
 *
 * Hardware connections:
 *   - GPIO 2  : Packet Received Activity LED (Green)
 *   - GPIO 4  : Alert / Emergency LED (Red)
 *   - USB TX/RX: Serial Monitor at 115200 baud
 *
 * Serial Commands:
 *   s = display comprehensive node health & link status
 *   c = clear alert statistics
 *   h = help menu
 * =====================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration Constants ---
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* MQTT_SERVER   = "broker.emqx.io";
const int   MQTT_PORT     = 1883;
const char* NODE_ID       = "GATEWAY";

// MQTT Topics
const char* TOPIC_SUB_R4A    = "mine/r4a/to/gateway";
const char* TOPIC_SUB_R4B    = "mine/r4b/to/gateway";
const char* TOPIC_SUB_HB     = "node/status/+";

const char* TOPIC_PUB_DATA   = "mine/gateway/data";
const char* TOPIC_PUB_ALERTS = "mine/gateway/alerts";
const char* TOPIC_PUB_HEALTH = "mine/gateway/node_health";
const char* TOPIC_STATUS     = "node/status/GATEWAY";

// Pin Assignments
const int PIN_LED_RX    = 2;
const int PIN_LED_ALERT = 4;

// Network Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Statistics
unsigned long packetsReceivedTotal = 0;
unsigned long packetsAccepted      = 0;
unsigned long duplicatesDropped    = 0;
unsigned long alertsCount          = 0;

// Duplicate Detection Ring Buffer
const int CACHE_SIZE = 60;
String seenPackets[CACHE_SIZE];
int cacheIdx = 0;

bool isDuplicate(const String& id) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (seenPackets[i] == id) return true;
  }
  return false;
}

void markSeen(const String& id) {
  seenPackets[cacheIdx] = id;
  cacheIdx = (cacheIdx + 1) % CACHE_SIZE;
}

// Node Heartbeat Tracking (ROVER, R1A, R1B, R2, R3, R4A, R4B)
struct NodeTracker {
  const char* name;
  unsigned long lastSeenMs;
  bool isOnline;
};

NodeTracker trackedNodes[] = {
  { "ROVER", 0, false },
  { "R1A",   0, false },
  { "R1B",   0, false },
  { "R2",    0, false },
  { "R3",    0, false },
  { "R4A",   0, false },
  { "R4B",   0, false }
};
const int NUM_TRACKED_NODES = sizeof(trackedNodes) / sizeof(trackedNodes[0]);
const unsigned long NODE_TIMEOUT_MS = 5000; // 5 seconds timeout for stale node detection

unsigned long lastHealthPublishMs = 0;
unsigned long lastGatewayHbMs     = 0;

void publishGatewayHeartbeat() {
  StaticJsonDocument<128> doc;
  doc["node"] = NODE_ID;
  doc["online"] = true;
  doc["rx_count"] = packetsAccepted;
  doc["uptime_s"] = millis() / 1000;

  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(TOPIC_STATUS, buffer);
}

void evaluateNodeHeartbeats() {
  unsigned long now = millis();
  StaticJsonDocument<512> healthDoc;
  healthDoc["gateway"] = true;

  JsonObject nodesObj = healthDoc.createNestedObject("nodes");

  for (int i = 0; i < NUM_TRACKED_NODES; i++) {
    bool previouslyOnline = trackedNodes[i].isOnline;
    if (trackedNodes[i].lastSeenMs > 0 && (now - trackedNodes[i].lastSeenMs <= NODE_TIMEOUT_MS)) {
      trackedNodes[i].isOnline = true;
    } else {
      trackedNodes[i].isOnline = false;
      if (previouslyOnline) {
        Serial.printf("[GATEWAY ALARM] >>> Node '%s' HEARTBEAT TIMEOUT! Node marked OFFLINE <<<\n",
                      trackedNodes[i].name);
      }
    }
    nodesObj[trackedNodes[i].name] = trackedNodes[i].isOnline;
  }

  char buffer[512];
  serializeJson(healthDoc, buffer);
  client.publish(TOPIC_PUB_HEALTH, buffer);
}

void handleHeartbeatMessage(char* topic, byte* payload, unsigned int length) {
  // Topic format: node/status/<NODE_NAME>
  String topicStr = String(topic);
  String nodeName = topicStr.substring(topicStr.lastIndexOf('/') + 1);

  for (int i = 0; i < NUM_TRACKED_NODES; i++) {
    if (nodeName.equalsIgnoreCase(trackedNodes[i].name)) {
      trackedNodes[i].lastSeenMs = millis();
      trackedNodes[i].isOnline = true;
      break;
    }
  }
}

void handleDataPacket(char* topic, byte* payload, unsigned int length) {
  packetsReceivedTotal++;

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[GATEWAY ERROR] Corrupted JSON payload from topic: %s\n", topic);
    return;
  }

  String sourceNode = doc["node"] | "ROVER";
  unsigned long seq = doc["seq"] | 0;
  unsigned long timestamp = doc["timestamp"] | 0;
  float methane = doc["methane"] | 0.0;
  String methaneStatus = doc["methane_status"] | "NORMAL";
  float temp = doc["temperature"] | 0.0;
  float humidity = doc["humidity"] | 0.0;
  int water = doc["water"] | 0;
  int person = doc["person"] | 0;
  int battery = doc["battery"] | 0;
  int alert = doc["alert"] | 0;
  String path = doc["path"] | "UNKNOWN";

  // Duplicate filtering:
  // Both R4A and R4B will send packets if both network branches are functioning.
  // The gateway keeps the first one and filters the duplicate!
  String packetId = sourceNode + "_" + String(seq);
  if (isDuplicate(packetId)) {
    duplicatesDropped++;
    Serial.printf("[GATEWAY DEDUPLICATION] Packet #%lu via '%s' dropped (already received via alternate branch).\n",
                  seq, path.c_str());
    return;
  }

  // Record this sequence number as processed
  markSeen(packetId);
  packetsAccepted++;

  // LED indication
  digitalWrite(PIN_LED_RX, HIGH);

  // Append GATEWAY to path for final dashboard display
  String finalPath = path + ">GATEWAY";
  doc["path"] = finalPath;

  // Format serial output strictly matching requested format:
  Serial.println("--------------------------------");
  Serial.println("GATEWAY PACKET");
  Serial.printf("Sequence: %lu\n", seq);
  Serial.printf("Path: %s\n", finalPath.c_str());
  if (methane < 0 || methaneStatus == "NOT MEASURED") {
    Serial.println("Methane: NOT MEASURED");
  } else {
    Serial.printf("Methane: %.1f %%LEL\n", methane);
  }
  Serial.printf("Temperature: %.1f C\n", temp);
  Serial.printf("Humidity: %.0f %%\n", humidity);
  Serial.printf("Water: %s\n", (water == 1) ? "FLOOD DETECTED" : "NORMAL");
  Serial.printf("Person: %s\n", (person == 1) ? "POSSIBLE PERSON" : "NOT DETECTED");
  Serial.printf("Battery: %d %%\n", battery);
  Serial.printf("Alert: %s\n", (alert == 1) ? "EMERGENCY ACTIVE" : "NORMAL");
  Serial.println("--------------------------------");

  // Alert LED update
  if (alert == 1) {
    alertsCount++;
    digitalWrite(PIN_LED_ALERT, HIGH);

    // Publish specific alert packet
    StaticJsonDocument<256> alertDoc;
    alertDoc["seq"] = seq;
    alertDoc["alert"] = 1;
    alertDoc["methane"] = methane;
    alertDoc["water"] = water;
    alertDoc["person"] = person;
    alertDoc["path"] = finalPath;
    alertDoc["timestamp"] = millis();

    String reason = "";
    if (methane >= 25.0) reason += "[METHANE DANGER] ";
    if (water == 1) reason += "[WATER DETECTED] ";
    if (person == 1) reason += "[POSSIBLE PERSON] ";
    alertDoc["reason"] = reason;

    char alertBuffer[256];
    serializeJson(alertDoc, alertBuffer);
    client.publish(TOPIC_PUB_ALERTS, alertBuffer);
  } else {
    digitalWrite(PIN_LED_ALERT, LOW);
  }

  // Publish final consolidated packet for the Laptop Dashboard
  char outBuffer[512];
  serializeJson(doc, outBuffer);
  client.publish(TOPIC_PUB_DATA, outBuffer);

  delay(25);
  digitalWrite(PIN_LED_RX, LOW);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strncmp(topic, "node/status/", 12) == 0) {
    handleHeartbeatMessage(topic, payload, length);
  } else {
    handleDataPacket(topic, payload, length);
  }
}

void setupWifi() {
  delay(10);
  Serial.printf("\n[GATEWAY] Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[GATEWAY] Wi-Fi connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[GATEWAY] Wi-Fi connection timed out. Retrying in loop.");
  }
}

void reconnectMqtt() {
  while (!client.connected()) {
    Serial.printf("[GATEWAY] Connecting to MQTT broker %s...", MQTT_SERVER);
    String clientId = "MineGateway-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" CONNECTED!");
      client.subscribe(TOPIC_SUB_R4A);
      client.subscribe(TOPIC_SUB_R4B);
      client.subscribe(TOPIC_SUB_HB);
      Serial.printf("[GATEWAY] Subscribed to:\n  - %s\n  - %s\n  - %s\n",
                    TOPIC_SUB_R4A, TOPIC_SUB_R4B, TOPIC_SUB_HB);
      publishGatewayHeartbeat();
    } else {
      Serial.printf(" Failed, rc=%d. Retrying in 2 seconds...\n", client.state());
      delay(2000);
    }
  }
}

void printStatus() {
  Serial.println("\n================ [GATEWAY STATUS & LINK HEALTH] ================");
  Serial.printf("  Total RX Packets     : %lu\n", packetsReceivedTotal);
  Serial.printf("  Accepted Packets     : %lu\n", packetsAccepted);
  Serial.printf("  Duplicates Filtered  : %lu\n", duplicatesDropped);
  Serial.printf("  Emergency Alerts RX  : %lu\n", alertsCount);
  Serial.println("  -------------------------------------------------------------");
  Serial.println("  NODE HEARTBEAT & AVAILABILITY TABLE:");
  unsigned long now = millis();
  for (int i = 0; i < NUM_TRACKED_NODES; i++) {
    bool online = (trackedNodes[i].lastSeenMs > 0) && (now - trackedNodes[i].lastSeenMs <= NODE_TIMEOUT_MS);
    long diffSec = (trackedNodes[i].lastSeenMs == 0) ? -1 : (long)((now - trackedNodes[i].lastSeenMs) / 1000);
    Serial.printf("    %-8s : %-8s (Last heartbeat: %ld s ago)\n",
                  trackedNodes[i].name,
                  online ? "ONLINE" : "OFFLINE",
                  diffSec);
  }
  Serial.printf("    %-8s : ONLINE   (Local Node)\n", NODE_ID);
  Serial.println("=================================================================\n");
}

void printHelp() {
  Serial.println("\n--- GATEWAY SERIAL COMMANDS ---");
  Serial.println("  s : Display full network topology health & statistics");
  Serial.println("  c : Clear counters");
  Serial.println("  h : Print this help menu");
  Serial.println("--------------------------------\n");
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') return;

    if (cmd == 's' || cmd == 'S') {
      printStatus();
    } else if (cmd == 'c' || cmd == 'C') {
      packetsReceivedTotal = 0;
      packetsAccepted = 0;
      duplicatesDropped = 0;
      alertsCount = 0;
      Serial.println("[GATEWAY] Statistics counters reset.");
    } else if (cmd == 'h' || cmd == 'H') {
      printHelp();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=============================================");
  Serial.println("  SIH 2026 MINE RESCUE ROVER - BASE GATEWAY  ");
  Serial.println("=============================================");

  pinMode(PIN_LED_RX, OUTPUT);
  pinMode(PIN_LED_ALERT, OUTPUT);

  digitalWrite(PIN_LED_RX, LOW);
  digitalWrite(PIN_LED_ALERT, LOW);

  for (int i = 0; i < CACHE_SIZE; i++) seenPackets[i] = "";

  setupWifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);

  printHelp();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setupWifi();
  }

  if (!client.connected()) {
    reconnectMqtt();
  }
  client.loop();

  handleSerialInput();

  unsigned long currentMs = millis();

  // Periodic Gateway Heartbeat (every 3 seconds)
  if (currentMs - lastGatewayHbMs >= 3000) {
    lastGatewayHbMs = currentMs;
    publishGatewayHeartbeat();
  }

  // Periodic Node Health Analysis & Broadcast (every 2.5 seconds)
  if (currentMs - lastHealthPublishMs >= 2500) {
    lastHealthPublishMs = currentMs;
    evaluateNodeHeartbeats();
  }
}
