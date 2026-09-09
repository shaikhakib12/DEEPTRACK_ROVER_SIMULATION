/*
 * =====================================================================
 * SIH 2026 MINE RESCUE ROVER - R4B EXIT RELAY NODE
 * =====================================================================
 * Microcontroller : ESP32 DevKit V1 (Wokwi Simulation)
 * Role            : Exit Relay Branch B (Interfaces with Gateway)
 *
 * Subscribes to:
 *   - Topic: mine/r3/to/r4b
 * Forwards to:
 *   - Topic: mine/r4b/to/gateway
 * Heartbeat:
 *   - Topic: node/status/R4B
 *
 * Hardware connections:
 *   - GPIO 2  : Forwarding Activity LED (Green)
 *   - GPIO 4  : Heartbeat LED (Yellow)
 *   - GPIO 5  : Drop / Duplicate / Fault LED (Red)
 *   - GPIO 32 : Toggle Fault Pushbutton (Simulate hardware failover)
 *
 * Loop Prevention & Deduplication:
 *   - Retains cache of (Source + SeqNumber).
 *   - Checks path: if "R4B" is already present, packet is DROPPED.
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
const char* NODE_ID       = "R4B";

// MQTT Topics
const char* TOPIC_SUB     = "mine/r3/to/r4b";
const char* TOPIC_PUB     = "mine/r4b/to/gateway";
const char* TOPIC_STATUS  = "node/status/R4B";

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

// Duplicate Detection Ring Buffer
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
    Serial.println("[R4B] [OFFLINE] Packet dropped (simulated failure mode).");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    packetsDropped++;
    Serial.printf("[R4B] [ERROR] JSON deserialization failed: %s\n", err.c_str());
    return;
  }

  String sourceNode = doc["node"] | "UNKNOWN";
  unsigned long seq = doc["seq"] | 0;
  String currentPath = doc["path"] | "";

  // Loop Prevention
  if (currentPath.indexOf("R4B") != -1) {
    packetsDropped++;
    Serial.printf("[R4B] [LOOP DETECTED] Packet already visited R4B (Path: %s). Dropped.\n", currentPath.c_str());
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    return;
  }

  // Duplicate Check
  String packetId = sourceNode + "_" + String(seq);
  if (isDuplicate(packetId)) {
    duplicatesCount++;
    packetsDropped++;
    Serial.printf("[R4B] [DUPLICATE] Packet ID '%s' already handled. Dropped.\n", packetId.c_str());
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    return;
  }

  addToCache(packetId);

  if (currentPath.length() == 0) {
    currentPath = "ROVER";
  }
  currentPath += ">";
  currentPath += NODE_ID;
  doc["path"] = currentPath;

  char outBuffer[512];
  serializeJson(doc, outBuffer);

  digitalWrite(PIN_LED_FWD, HIGH);
  bool success = client.publish(TOPIC_PUB, outBuffer);
  delay(30);
  digitalWrite(PIN_LED_FWD, LOW);

  if (success) {
    packetsForwarded++;
    Serial.println("--------------------------------------------------");
    Serial.printf("[R4B FORWARDED TO GATEWAY] Seq #%lu | Path: %s\n", seq, currentPath.c_str());
    Serial.printf("  Sub: %s  ==>  Pub: %s\n", topic, TOPIC_PUB);
  } else {
    packetsDropped++;
    Serial.println("[R4B] [ERROR] MQTT publish to Gateway failed!");
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
      client.subscribe(TOPIC_SUB);
      Serial.printf("[%s] Subscribed to topic: %s\n", NODE_ID, TOPIC_SUB);
      publishHeartbeat();
    } else {
      Serial.printf(" Failed, rc=%d. Retrying in 2 seconds...\n", client.state());
      delay(2000);
    }
  }
}

void printStatus() {
  Serial.println("\n========== [R4B RELAY STATUS] ==========");
  Serial.printf("  State           : %s\n", (isFaultOffline ? "OFFLINE / FAULT (DEAD)" : "ONLINE / ACTIVE"));
  Serial.printf("  Packets In      : %lu\n", packetsReceived);
  Serial.printf("  Packets Fwd     : %lu\n", packetsForwarded);
  Serial.printf("  Packets Dropped : %lu\n", packetsDropped);
  Serial.printf("  Duplicates Det. : %lu\n", duplicatesCount);
  Serial.printf("  MQTT Status     : %s\n", (client.connected() ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("========================================\n");
}

void printHelp() {
  Serial.println("\n--- R4B EXIT RELAY COMMANDS ---");
  Serial.println("  f : Toggle Fault / Offline (Simulate Relay Failure)");
  Serial.println("  s : Display relay statistics");
  Serial.println("  h : Print this help menu");
  Serial.println("-------------------------------\n");
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') return;

    if (cmd == 'f' || cmd == 'F') {
      isFaultOffline = !isFaultOffline;
      if (isFaultOffline) {
        digitalWrite(PIN_LED_FAULT, HIGH);
        Serial.println("\n[SIMULATION] >>> R4B IS NOW OFFLINE (SIMULATED FAILURE)! <<<");
        Serial.println("Exit Relay R4B will drop packets. Exit Relay R4A will sustain delivery to Gateway.");
      } else {
        digitalWrite(PIN_LED_FAULT, LOW);
        Serial.println("\n[SIMULATION] >>> R4B RESTORED TO ONLINE STATE! <<<");
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
      Serial.printf("\n[BUTTON] Fault button toggled R4B state: %s\n",
                    isFaultOffline ? "OFFLINE (DEAD)" : "ONLINE (ACTIVE)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=============================================");
  Serial.println("  SIH 2026 MINE RESCUE ROVER - R4B EXIT RELAY");
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
