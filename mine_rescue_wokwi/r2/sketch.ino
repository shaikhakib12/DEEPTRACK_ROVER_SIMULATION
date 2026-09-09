/*
 * =====================================================================
 * SIH 2026 MINE RESCUE ROVER - R2 MIDDLE RELAY NODE (CROSS-LINKED)
 * =====================================================================
 * Microcontroller : ESP32 DevKit V1 (Wokwi Simulation)
 * Role            : Middle Relay Node (Left Branch with Cross-Link to R3)
 *
 * Subscribes to:
 *   - Topic 1: mine/r1a/to/r2   (Direct from R1A)
 *   - Topic 2: mine/r3/to/r2    (Cross-link from R3)
 * Forwards to:
 *   - Primary  : mine/r2/to/r4a (Downstream to Exit Relay R4A)
 *   - Crosslink: mine/r2/to/r3  (Cross-link to R3 if path hasn't visited R3)
 * Heartbeat:
 *   - Topic: node/status/R2
 *
 * Hardware connections:
 *   - GPIO 2  : Forwarding Activity LED (Green)
 *   - GPIO 4  : Heartbeat LED (Yellow)
 *   - GPIO 5  : Drop / Duplicate / Fault LED (Red)
 *   - GPIO 32 : Toggle Fault Pushbutton (Simulate hardware failover)
 *
 * Loop Prevention & Deduplication:
 *   - Retains cache of (Source + SeqNumber).
 *   - Analyzes "path" string: if "R2" is already present, packet is DROPPED.
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
const char* NODE_ID       = "R2";

// MQTT Topics
const char* TOPIC_SUB_R1A    = "mine/r1a/to/r2";
const char* TOPIC_SUB_R3     = "mine/r3/to/r2";
const char* TOPIC_PUB_R4A    = "mine/r2/to/r4a";
const char* TOPIC_PUB_R3     = "mine/r2/to/r3";
const char* TOPIC_STATUS     = "node/status/R2";

// Pin Assignments
const int PIN_LED_FWD   = 2;
const int PIN_LED_HB    = 4;
const int PIN_LED_FAULT = 5;
const int PIN_BTN_FAULT = 32;

// Network Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Relay Diagnostics
bool isFaultOffline = false;
unsigned long packetsReceived  = 0;
unsigned long packetsForwarded = 0;
unsigned long packetsDropped   = 0;
unsigned long duplicatesCount  = 0;
unsigned long lastHeartbeatMs  = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;

// Duplicate Detection Ring Buffer (Composite Packet ID: "ROVER_123")
const int CACHE_SIZE = 50;
String packetIdCache[CACHE_SIZE];
int cacheIndex = 0;

bool isDuplicate(const String& packetId) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (packetIdCache[i].length() > 0 && packetIdCache[i] == packetId) {
      return true;
    }
  }
  return false;
}

void addToCache(const String& packetId) {
  packetIdCache[cacheIndex] = packetId;
  cacheIndex = (cacheIndex + 1) % CACHE_SIZE;
}

void publishHeartbeat() {
  if (isFaultOffline) return;

  StaticJsonDocument<128> doc;
  doc["node"] = NODE_ID;
  doc["online"] = true;
  doc["fwd_count"] = packetsForwarded;
  doc["uptime_s"] = millis() / 1000;

  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(TOPIC_STATUS, buffer);

  digitalWrite(PIN_LED_HB, HIGH);
  delay(20);
  digitalWrite(PIN_LED_HB, LOW);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  packetsReceived++;

  if (isFaultOffline) {
    packetsDropped++;
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    Serial.println("[R2] [OFFLINE] Packet ignored (simulated failure mode).");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    packetsDropped++;
    Serial.printf("[R2] [ERROR] JSON deserialization failed: %s\n", err.c_str());
    return;
  }

  String sourceNode = doc["node"] | "UNKNOWN";
  unsigned long seq = doc["seq"] | 0;
  String currentPath = doc["path"] | "";

  // 1. Strict Loop Prevention: Does the path already include R2?
  if (currentPath.indexOf("R2") != -1) {
    packetsDropped++;
    Serial.printf("[R2] [LOOP DETECTED] Packet has already visited R2 (Path: %s). Dropping!\n", currentPath.c_str());
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    return;
  }

  // 2. Duplicate Detection: Check source + sequence cache
  String packetId = sourceNode + "_" + String(seq);
  if (isDuplicate(packetId)) {
    duplicatesCount++;
    packetsDropped++;
    Serial.printf("[R2] [DUPLICATE] Packet ID '%s' already handled. Dropping.\n", packetId.c_str());
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    return;
  }

  // Register in deduplication ring buffer
  addToCache(packetId);

  // 3. Append R2 to the transmission path
  if (currentPath.length() == 0) {
    currentPath = "ROVER";
  }
  currentPath += ">";
  currentPath += NODE_ID;
  doc["path"] = currentPath;

  char outBuffer[512];
  serializeJson(doc, outBuffer);

  digitalWrite(PIN_LED_FWD, HIGH);

  // 4. Primary Downstream Forwarding to Exit Relay R4A
  bool pubDownstream = client.publish(TOPIC_PUB_R4A, outBuffer);

  // 5. Cross-Link Forwarding to Middle Relay R3
  // Only cross-link if packet arrived from R1A AND has not traversed R3 yet!
  bool pubCrosslink = false;
  if (strcmp(topic, TOPIC_SUB_R1A) == 0 && currentPath.indexOf("R3") == -1) {
    pubCrosslink = client.publish(TOPIC_PUB_R3, outBuffer);
  }

  delay(30);
  digitalWrite(PIN_LED_FWD, LOW);

  if (pubDownstream || pubCrosslink) {
    packetsForwarded++;
    Serial.println("--------------------------------------------------");
    Serial.printf("[R2 FORWARDED] Seq #%lu | Path: %s\n", seq, currentPath.c_str());
    Serial.printf("  In: %s  ==>  Out Downstream (R4A): %s | Cross-Link (R3): %s\n",
                  topic,
                  pubDownstream ? "SENT" : "N/A",
                  pubCrosslink ? "SENT" : "SKIPPED");
  } else {
    packetsDropped++;
    Serial.println("[R2] [ERROR] Forwarding failed!");
  }
}

void setupWifi() {
  delay(10);
  Serial.printf("\n[%s] Connecting to Wi-Fi SSID: %s\n", NODE_ID, WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[%s] Wi-Fi connected! IP: %s\n", NODE_ID, WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("\n[%s] Wi-Fi connection timed out. Retrying in loop.\n", NODE_ID);
  }
}

void reconnectMqtt() {
  while (!client.connected()) {
    Serial.printf("[%s] Connecting to MQTT broker %s...", NODE_ID, MQTT_SERVER);
    String clientId = "MineRelay-" + String(NODE_ID) + "-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" CONNECTED!");
      client.subscribe(TOPIC_SUB_R1A);
      client.subscribe(TOPIC_SUB_R3);
      Serial.printf("[%s] Subscribed to:\n  - %s\n  - %s\n", NODE_ID, TOPIC_SUB_R1A, TOPIC_SUB_R3);
      publishHeartbeat();
    } else {
      Serial.printf(" Failed, rc=%d. Retrying in 2 seconds...\n", client.state());
      delay(2000);
    }
  }
}

void printStatus() {
  Serial.println("\n========== [R2 RELAY STATUS] ==========");
  Serial.printf("  State           : %s\n", (isFaultOffline ? "OFFLINE / FAULT (DEAD)" : "ONLINE / ACTIVE"));
  Serial.printf("  Packets In      : %lu\n", packetsReceived);
  Serial.printf("  Packets Fwd     : %lu\n", packetsForwarded);
  Serial.printf("  Packets Dropped : %lu\n", packetsDropped);
  Serial.printf("  Duplicates Det. : %lu\n", duplicatesCount);
  Serial.printf("  MQTT Status     : %s\n", (client.connected() ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("========================================\n");
}

void printHelp() {
  Serial.println("\n--- R2 MIDDLE RELAY COMMANDS ---");
  Serial.println("  f : Toggle Fault / Offline (Simulate Relay Failure)");
  Serial.println("  s : Display relay statistics");
  Serial.println("  h : Print this help menu");
  Serial.println("--------------------------------\n");
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') return;

    if (cmd == 'f' || cmd == 'F') {
      isFaultOffline = !isFaultOffline;
      if (isFaultOffline) {
        digitalWrite(PIN_LED_FAULT, HIGH);
        Serial.println("\n[SIMULATION] >>> R2 IS NOW OFFLINE (SIMULATED FAILURE)! <<<");
        Serial.println("Packets through R2 will cease. Cross-linked R3 will sustain communication.");
      } else {
        digitalWrite(PIN_LED_FAULT, LOW);
        Serial.println("\n[SIMULATION] >>> R2 RESTORED TO ONLINE STATE! <<<");
      }
    } else if (cmd == 's' || cmd == 'S') {
      printStatus();
    } else if (cmd == 'h' || cmd == 'H') {
      printHelp();
    }
  }
}

unsigned long lastBtnPressMs = 0;
void checkFaultButton() {
  if (digitalRead(PIN_BTN_FAULT) == LOW) {
    if (millis() - lastBtnPressMs > 400) {
      lastBtnPressMs = millis();
      isFaultOffline = !isFaultOffline;
      digitalWrite(PIN_LED_FAULT, isFaultOffline ? HIGH : LOW);
      Serial.printf("\n[BUTTON] Fault button toggled R2 state: %s\n",
                    isFaultOffline ? "OFFLINE (DEAD)" : "ONLINE (ACTIVE)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=============================================");
  Serial.println("  SIH 2026 MINE RESCUE ROVER - R2 MIDDLE RELAY");
  Serial.println("=============================================");

  pinMode(PIN_LED_FWD, OUTPUT);
  pinMode(PIN_LED_HB, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);
  pinMode(PIN_BTN_FAULT, INPUT_PULLUP);

  digitalWrite(PIN_LED_FWD, LOW);
  digitalWrite(PIN_LED_HB, LOW);
  digitalWrite(PIN_LED_FAULT, LOW);

  for (int i = 0; i < CACHE_SIZE; i++) packetIdCache[i] = "";

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
  checkFaultButton();

  unsigned long currentMs = millis();
  if (currentMs - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
    lastHeartbeatMs = currentMs;
    publishHeartbeat();
  }
}
