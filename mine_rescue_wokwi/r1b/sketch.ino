/*
 * =====================================================================
 * SIH 2026 MINE RESCUE ROVER - R1B ENTRY RELAY NODE
 * =====================================================================
 * Microcontroller : ESP32 DevKit V1 (Wokwi Simulation)
 * Role            : Redundant Entry Relay Branch B
 *
 * Subscribes to:
 *   - Topic: mine/rover/to/r1b
 * Forwards to:
 *   - Topic: mine/r1b/to/r3
 * Heartbeat:
 *   - Topic: node/status/R1B
 *
 * Hardware connections:
 *   - GPIO 2  : Packet Forwarding Activity LED (Green)
 *   - GPIO 4  : Heartbeat / Power LED (Yellow)
 *   - GPIO 5  : Drop / Offline Fault LED (Red)
 *   - GPIO 32 : Toggle Fault Pushbutton (Simulate hardware death/failover)
 *
 * Serial Commands:
 *   f = toggle Fail / Offline state (demonstrates automatic path failover)
 *   s = display relay statistics
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
const char* NODE_ID       = "R1B";

// MQTT Topics
const char* TOPIC_SUB     = "mine/rover/to/r1b";
const char* TOPIC_PUB     = "mine/r1b/to/r3";
const char* TOPIC_STATUS  = "node/status/R1B";

// Pin Assignments
const int PIN_LED_FWD   = 2;
const int PIN_LED_HB    = 4;
const int PIN_LED_FAULT = 5;
const int PIN_BTN_FAULT = 32;

// Network Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Relay State & Diagnostics
bool isFaultOffline = false;
unsigned long packetsReceived  = 0;
unsigned long packetsForwarded = 0;
unsigned long packetsDropped   = 0;
unsigned long duplicatesCount  = 0;
unsigned long lastHeartbeatMs  = 0;
const unsigned long HEARTBEAT_INTERVAL = 3000;

// Duplicate Detection Ring Buffer
const int CACHE_SIZE = 40;
unsigned long seqCache[CACHE_SIZE];
int cacheIndex = 0;

bool isDuplicate(unsigned long seq) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (seqCache[i] == seq) {
      return true;
    }
  }
  return false;
}

void addToCache(unsigned long seq) {
  seqCache[cacheIndex] = seq;
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
    delay(30);
    digitalWrite(PIN_LED_FAULT, LOW);
    Serial.println("[R1B] [OFFLINE] Packet dropped because node is simulated FAULT/OFFLINE.");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    packetsDropped++;
    Serial.printf("[R1B] [ERROR] JSON deserialization failed: %s\n", err.c_str());
    return;
  }

  unsigned long seq = doc["seq"] | 0;

  if (isDuplicate(seq)) {
    duplicatesCount++;
    packetsDropped++;
    Serial.printf("[R1B] [DUPLICATE DROPPED] Seq #%lu already forwarded. Ignored.\n", seq);
    digitalWrite(PIN_LED_FAULT, HIGH);
    delay(20);
    digitalWrite(PIN_LED_FAULT, LOW);
    return;
  }

  addToCache(seq);

  String currentPath = doc["path"].as<String>();
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
    Serial.printf("[R1B FORWARDED] Seq #%lu | Path: %s\n", seq, currentPath.c_str());
    Serial.printf("  Sub: %s  ==>  Fwd: %s\n", topic, TOPIC_PUB);
  } else {
    packetsDropped++;
    Serial.println("[R1B] [ERROR] MQTT publish to R3 failed!");
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
  Serial.println("\n========== [R1B RELAY STATUS] ==========");
  Serial.printf("  State           : %s\n", (isFaultOffline ? "OFFLINE / FAULT (DEAD)" : "ONLINE / ACTIVE"));
  Serial.printf("  Packets In      : %lu\n", packetsReceived);
  Serial.printf("  Packets Fwd     : %lu\n", packetsForwarded);
  Serial.printf("  Packets Dropped : %lu\n", packetsDropped);
  Serial.printf("  Duplicates Det. : %lu\n", duplicatesCount);
  Serial.printf("  MQTT Status     : %s\n", (client.connected() ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("========================================\n");
}

void printHelp() {
  Serial.println("\n--- R1B RELAY COMMANDS ---");
  Serial.println("  f : Toggle Fault / Offline (Simulate Relay Cutoff / Failure)");
  Serial.println("  s : Display relay statistics");
  Serial.println("  h : Print this help menu");
  Serial.println("---------------------------\n");
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') return;

    if (cmd == 'f' || cmd == 'F') {
      isFaultOffline = !isFaultOffline;
      if (isFaultOffline) {
        digitalWrite(PIN_LED_FAULT, HIGH);
        Serial.println("\n[SIMULATION] >>> R1B IS NOW OFFLINE (SIMULATED FAILURE)! <<<");
        Serial.println("Rover packets on Branch B will be DROPPED. Branch A (R1A) will take over.");
      } else {
        digitalWrite(PIN_LED_FAULT, LOW);
        Serial.println("\n[SIMULATION] >>> R1B RESTORED TO ONLINE STATE! <<<");
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
      Serial.printf("\n[BUTTON] Fault button toggled R1B state: %s\n",
                    isFaultOffline ? "OFFLINE (DEAD)" : "ONLINE (ACTIVE)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=============================================");
  Serial.println("  SIH 2026 MINE RESCUE ROVER - R1B RELAY     ");
  Serial.println("=============================================");

  pinMode(PIN_LED_FWD, OUTPUT);
  pinMode(PIN_LED_HB, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);
  pinMode(PIN_BTN_FAULT, INPUT_PULLUP);

  digitalWrite(PIN_LED_FWD, LOW);
  digitalWrite(PIN_LED_HB, LOW);
  digitalWrite(PIN_LED_FAULT, LOW);

  for (int i = 0; i < CACHE_SIZE; i++) seqCache[i] = 0;

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
