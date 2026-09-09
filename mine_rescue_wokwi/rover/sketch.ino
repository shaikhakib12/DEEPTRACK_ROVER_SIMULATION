/*
 * =====================================================================
 * SIH 2026 MINE RESCUE ROVER - ROVER SENSING NODE
 * =====================================================================
 * Microcontroller : ESP32 DevKit V1 / NodeMCU ESP32 (Wokwi Simulation)
 * Role            : Sensor acquisition, telemetry generation, dual-entry TX
 *
 * Targets:
 *   - Entry Relay 1A (Topic: mine/rover/to/r1a)
 *   - Entry Relay 1B (Topic: mine/rover/to/r1b)
 * Heartbeat:
 *   - Topic: node/status/ROVER
 *
 * Hardware connections:
 *   - GPIO 2  : Heartbeat / TX activity LED (Blue / Green)
 *   - GPIO 4  : Alert / Emergency LED (Red)
 *   - GPIO 34 : Potentiometer 1 (Methane Gas %LEL)
 *   - GPIO 35 : Potentiometer 2 (Ambient Temp / Humidity)
 *   - GPIO 32 : Pushbutton (Water flooding detector - active LOW)
 *   - GPIO 33 : Pushbutton (Person detection / SOS trigger - active LOW)
 *
 * Serial Commands (115200 baud):
 *   m = trigger dangerous Methane emergency (alert = 1)
 *   w = trigger Water flooding detected (alert = 1)
 *   p = trigger Possible Person detected (alert = 1)
 *   f = simulate Methane Sensor Failure (NOT MEASURED state)
 *   n = return to Normal safe simulated readings
 *   s = Show current status in Serial Monitor
 *   h = Help menu
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
const char* NODE_ID       = "ROVER";

// MQTT Topics
const char* TOPIC_TO_R1A   = "mine/rover/to/r1a";
const char* TOPIC_TO_R1B   = "mine/rover/to/r1b";
const char* TOPIC_STATUS   = "node/status/ROVER";

// Pin Assignments
const int PIN_LED_TX     = 2;
const int PIN_LED_ALERT  = 4;
const int PIN_POT_GAS    = 34;
const int PIN_POT_ENV    = 35;
const int PIN_BTN_WATER  = 32;
const int PIN_BTN_PERSON = 33;

// Thresholds
const float METHANE_ALERT_THRESHOLD = 25.0; // %LEL

// Network Clients
WiFiClient espClient;
PubSubClient client(espClient);

// State Variables
unsigned long seqNumber       = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastHeartbeatMs = 0;
const unsigned long TELEMETRY_INTERVAL = 2000; // 2 seconds
const unsigned long HEARTBEAT_INTERVAL = 3000; // 3 seconds

// Simulation & Override Flags
bool manualMethaneActive = false;
bool manualWaterActive   = false;
bool manualPersonActive  = false;
bool sensorFailureActive = false;
int  simulatedBattery    = 92;
unsigned long lastBatteryDrainMs = 0;

void setupWifi() {
  delay(10);
  Serial.println();
  Serial.print("[ROVER] Connecting to Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[ROVER] Wi-Fi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[ROVER] Wi-Fi connection timed out. Will retry in loop.");
  }
}

void reconnectMqtt() {
  while (!client.connected()) {
    Serial.print("[ROVER] Attempting MQTT connection to ");
    Serial.print(MQTT_SERVER);
    Serial.print("...");
    
    String clientId = "MineRover-" + String(NODE_ID) + "-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println(" CONNECTED!");
      publishHeartbeat();
    } else {
      Serial.print(" Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 2 seconds...");
      delay(2000);
    }
  }
}

void publishHeartbeat() {
  StaticJsonDocument<128> doc;
  doc["node"] = NODE_ID;
  doc["online"] = true;
  doc["uptime_s"] = millis() / 1000;

  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(TOPIC_STATUS, buffer);
  // Serial.println("[ROVER] Heartbeat published.");
}

void readSensors(float &methane, String &methaneStatus, float &temp, float &humidity, int &water, int &person, int &alert) {
  // 1. Methane Gas (%LEL)
  if (sensorFailureActive) {
    methane = -1.0;
    methaneStatus = "NOT MEASURED";
  } else if (manualMethaneActive) {
    methane = 68.5; // Dangerous gas burst
    methaneStatus = "ALERT";
  } else {
    int rawGas = analogRead(PIN_POT_GAS);
    // Map 0..4095 to 0.0 .. 60.0 %LEL
    methane = (rawGas / 4095.0) * 60.0;
    // Add minor realistic noise
    methane += (random(-5, 6) / 10.0);
    if (methane < 0) methane = 0;
    methaneStatus = (methane >= METHANE_ALERT_THRESHOLD) ? "ALERT" : "NORMAL";
  }

  // 2. Ambient Temp & Humidity
  int rawEnv = analogRead(PIN_POT_ENV);
  temp = 20.0 + (rawEnv / 4095.0) * 30.0 + (random(-3, 4) / 10.0); // 20 - 50 C
  humidity = 40.0 + (rawEnv / 4095.0) * 45.0 + (random(-5, 6) / 10.0); // 40 - 85 %

  // 3. Water Flooding Detector (Button on GPIO 32 is active LOW)
  bool btnWaterPressed = (digitalRead(PIN_BTN_WATER) == LOW);
  water = (manualWaterActive || btnWaterPressed) ? 1 : 0;

  // 4. Person Detection (Button on GPIO 33 is active LOW)
  bool btnPersonPressed = (digitalRead(PIN_BTN_PERSON) == LOW);
  person = (manualPersonActive || btnPersonPressed) ? 1 : 0;

  // 5. Alert Logic:
  // Independent Gas & Water safety triggers immediately without waiting for AI!
  if ((methane >= METHANE_ALERT_THRESHOLD) || (water == 1) || (person == 1)) {
    alert = 1;
    digitalWrite(PIN_LED_ALERT, HIGH);
  } else {
    alert = 0;
    digitalWrite(PIN_LED_ALERT, LOW);
  }
}

void sendTelemetryPacket() {
  float methane = 0.0;
  String methaneStatus = "NORMAL";
  float temp = 0.0;
  float humidity = 0.0;
  int water = 0;
  int person = 0;
  int alert = 0;

  readSensors(methane, methaneStatus, temp, humidity, water, person, alert);

  // Slow battery drain simulation
  if (millis() - lastBatteryDrainMs > 45000) {
    lastBatteryDrainMs = millis();
    if (simulatedBattery > 15) simulatedBattery--;
  }

  seqNumber++;

  StaticJsonDocument<384> doc;
  doc["node"]           = NODE_ID;
  doc["seq"]            = seqNumber;
  doc["timestamp"]      = millis();
  if (sensorFailureActive) {
    doc["methane"]      = -1;
  } else {
    doc["methane"]      = serialized(String(methane, 1));
  }
  doc["methane_status"] = methaneStatus;
  doc["temperature"]    = serialized(String(temp, 1));
  doc["humidity"]       = serialized(String(humidity, 1));
  doc["water"]          = water;
  doc["person"]         = person;
  doc["battery"]        = simulatedBattery;
  doc["alert"]          = alert;
  doc["path"]           = "ROVER";

  char jsonBuffer[384];
  serializeJson(doc, jsonBuffer);

  // Activity LED flash
  digitalWrite(PIN_LED_TX, HIGH);

  // Publish to BOTH redundant entry relays simultaneously!
  bool pubA = client.publish(TOPIC_TO_R1A, jsonBuffer);
  bool pubB = client.publish(TOPIC_TO_R1B, jsonBuffer);

  delay(40);
  digitalWrite(PIN_LED_TX, LOW);

  Serial.println("----------------------------------------");
  Serial.printf("[ROVER TX] Seq #%lu | Alert: %d | Gas: %s | Water: %d | Person: %d\n",
                seqNumber, alert, (sensorFailureActive ? "NOT MEASURED" : String(methane, 1).c_str()), water, person);
  Serial.printf("  TX -> %s [%s]\n", TOPIC_TO_R1A, pubA ? "OK" : "FAIL");
  Serial.printf("  TX -> %s [%s]\n", TOPIC_TO_R1B, pubB ? "OK" : "FAIL");
}

void printHelp() {
  Serial.println("\n========== ROVER SERIAL COMMAND MENU ==========");
  Serial.println("  m : Trigger Methane emergency (>25 %LEL, alert=1)");
  Serial.println("  w : Trigger Water flooding (water=1, alert=1)");
  Serial.println("  p : Trigger Possible Person detection (person=1, alert=1)");
  Serial.println("  f : Simulate Methane sensor failure (NOT MEASURED state)");
  Serial.println("  n : Return to Normal safe simulated values (alert=0)");
  Serial.println("  s : Show current sensor and network state");
  Serial.println("  h : Print this help menu");
  Serial.println("================================================\n");
}

void printStatus() {
  float m; String ms; float t, h; int w, p, a;
  readSensors(m, ms, t, h, w, p, a);
  Serial.println("\n--- [ROVER CURRENT STATE] ---");
  Serial.printf("  Sequence      : %lu\n", seqNumber);
  Serial.printf("  Methane       : %s %s\n", (sensorFailureActive ? "N/A" : String(m, 1).c_str()), ms.c_str());
  Serial.printf("  Temperature   : %.1f C\n", t);
  Serial.printf("  Humidity      : %.1f %%\n", h);
  Serial.printf("  Water Flood   : %s\n", (w ? "DETECTED (1)" : "NORMAL (0)"));
  Serial.printf("  Person AI     : %s\n", (p ? "POSSIBLE PERSON (1)" : "NO PERSON (0)"));
  Serial.printf("  Battery       : %d %%\n", simulatedBattery);
  Serial.printf("  Master Alert  : %s\n", (a ? "EMERGENCY (1)" : "NORMAL (0)"));
  Serial.printf("  WiFi Status   : %s\n", (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"));
  Serial.printf("  MQTT Status   : %s\n", (client.connected() ? "CONNECTED" : "DISCONNECTED"));
  Serial.println("-----------------------------\n");
}

void handleSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    // Ignore newline/carriage return
    if (cmd == '\r' || cmd == '\n') return;

    switch (cmd) {
      case 'm':
      case 'M':
        manualMethaneActive = true;
        sensorFailureActive = false;
        Serial.println("\n[CMD] >>> METHANE EMERGENCY TRIGGERED! <<<");
        sendTelemetryPacket();
        break;

      case 'w':
      case 'W':
        manualWaterActive = true;
        Serial.println("\n[CMD] >>> WATER FLOOD DETECTED! <<<");
        sendTelemetryPacket();
        break;

      case 'p':
      case 'P':
        manualPersonActive = true;
        Serial.println("\n[CMD] >>> POSSIBLE PERSON DETECTED (THERMAL AI)! <<<");
        sendTelemetryPacket();
        break;

      case 'f':
      case 'F':
        sensorFailureActive = !sensorFailureActive;
        manualMethaneActive = false;
        Serial.printf("\n[CMD] Methane Sensor Failure toggled: %s\n",
                      sensorFailureActive ? "ACTIVE (NOT MEASURED)" : "CLEARED");
        sendTelemetryPacket();
        break;

      case 'n':
      case 'N':
        manualMethaneActive = false;
        manualWaterActive   = false;
        manualPersonActive  = false;
        sensorFailureActive = false;
        Serial.println("\n[CMD] All overrides cleared. System restored to NORMAL.");
        sendTelemetryPacket();
        break;

      case 's':
      case 'S':
        printStatus();
        break;

      case 'h':
      case 'H':
        printHelp();
        break;

      default:
        Serial.printf("[CMD] Unknown command '%c'. Type 'h' for help.\n", cmd);
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=============================================");
  Serial.println("  SIH 2026 MINE RESCUE ROVER - ROVER NODE    ");
  Serial.println("=============================================");

  pinMode(PIN_LED_TX, OUTPUT);
  pinMode(PIN_LED_ALERT, OUTPUT);
  pinMode(PIN_BTN_WATER, INPUT_PULLUP);
  pinMode(PIN_BTN_PERSON, INPUT_PULLUP);

  digitalWrite(PIN_LED_TX, LOW);
  digitalWrite(PIN_LED_ALERT, LOW);

  setupWifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);

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

  // Periodic Telemetry TX
  if (currentMs - lastTelemetryMs >= TELEMETRY_INTERVAL) {
    lastTelemetryMs = currentMs;
    sendTelemetryPacket();
  }

  // Periodic Heartbeat
  if (currentMs - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
    lastHeartbeatMs = currentMs;
    publishHeartbeat();
  }
}
