#include <Wire.h>
#include <AccelStepper.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

// =====================================================
// 0. WIFI, MQTT, DAN WAKTU
// =====================================================
const char* ssid = "isi wifi"; // isi wifi
const char* password = "isi passwoord"; // isi password
const char* mqtt_server = "isi ipv4 yang wifi"; // isi ipv4 yang wifi
const uint16_t mqtt_port = 1883;

const char* TOPIC_SENSOR = "heliotrops/sensor_data";
const char* TOPIC_CONTROL = "heliotrops/control";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMqttReconnect = 0;
unsigned long lastWiFiAttempt = 0;

const unsigned long reconnectInterval = 5000;

bool timeSyncRequested = false;

// =====================================================
// 1. PIN CONFIGURATION
// =====================================================
const int ldrTop = 34;
const int ldrBottom = 35;
const int ldrLeft = 32;
const int ldrRight = 33;

#define M1_IN1 19
#define M1_IN2 18
#define M1_IN3 5
#define M1_IN4 17

#define M2_IN1 23
#define M2_IN2 16
#define M2_IN3 4
#define M2_IN4 2

#define BUTTON_PIN 13
#define SDA_PIN 21
#define SCL_PIN 22

// =====================================================
// 2. HARDWARE INITIALIZATION
// =====================================================
#define HALFSTEP 8
#define STEPS_PER_REVOLUTION 4096

AccelStepper motorHorizontal(
  HALFSTEP, M1_IN1, M1_IN3, M1_IN2, M1_IN4
);

AccelStepper motorVertical(
  HALFSTEP, M2_IN1, M2_IN3, M2_IN2, M2_IN4
);

Adafruit_INA219 ina219;
bool ina219Ready = false;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// =====================================================
// 3. TRACKING CONFIGURATION
// =====================================================
bool trackingEnabled = false;

const int threshold = 250;
const int correctionSteps = 16;

const unsigned long sensorInterval = 1000;

const int HORIZONTAL_DIRECTION = 1;
const int VERTICAL_DIRECTION = 1;

// =====================================================
// 4. BUTTON CONFIGURATION
// =====================================================
const int BUTTON_PRESSED = LOW;

int lastButtonReading = HIGH;
int stableButtonState = HIGH;

unsigned long lastDebounceTime = 0;

const unsigned long debounceDelay = 50;

// =====================================================
// 5. SENSOR VARIABLES
// =====================================================
int topValue = 0;
int bottomValue = 0;
int leftValue = 0;
int rightValue = 0;

int horizontalDelta = 0;
int verticalDelta = 0;
int lightIntensity = 0;

float busVoltage = 0;
float shuntVoltage_mV = 0;

float panelVoltage = 0;
float current_mA = 0;
float power_mW = 0;
float energy_Wh = 0;

unsigned long previousSensorTime = 0;
unsigned long previousLCDTime = 0;
unsigned long previousEnergyTime = 0;

// =====================================================
// 6. TIME HELPERS
// =====================================================
bool getMeasurementTimestamp(char* output, size_t outputSize) {
  time_t now = time(nullptr);

  // Epoch di atas 2023-01-01 dianggap waktu valid.
  if (now < 1672531200) {
    snprintf(output, outputSize, "TIME_UNSYNCED");
    return false;
  }

  struct tm timeInfo;

  localtime_r(&now, &timeInfo);

  strftime(
    output,
    outputSize,
    "%Y-%m-%dT%H:%M:%S%z",
    &timeInfo
  );

  return true;
}

void requestTimeSync() {
  if (WiFi.status() == WL_CONNECTED && !timeSyncRequested) {

    // Zona waktu WIB (UTC+7).
    configTzTime(
      "WIB-7",
      "pool.ntp.org",
      "time.google.com"
    );

    timeSyncRequested = true;

    Serial.println("[TIME] Meminta sinkronisasi waktu NTP...");
  }
}

// =====================================================
// 7. PACKET ID
// =====================================================
String getPacketId() {
  char idBuffer[48];

  uint32_t randomPart = esp_random();

  snprintf(
    idBuffer,
    sizeof(idBuffer),
    "%06X-%08lX-%08lX",
    (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF),
    (unsigned long)millis(),
    (unsigned long)randomPart
  );

  return String(idBuffer);
}

// =====================================================
// 8. MQTT CALLBACK: DASHBOARD CONTROL
// =====================================================
void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {
  String topicName(topic);

  if (topicName == TOPIC_CONTROL) {

    JsonDocument doc;

    DeserializationError error = deserializeJson(
      doc,
      payload,
      length
    );

    if (error) {
      Serial.printf(
        "[MQTT] JSON kontrol tidak valid: %s\n",
        error.c_str()
      );

      return;
    }

    const char* command = doc["command"] | "";

    if (strcmp(command, "ON") == 0) {

      trackingEnabled = true;

      Serial.println(
        "[MQTT CONTROL] Tracker AKTIF dari dashboard"
      );

    } else if (strcmp(command, "OFF") == 0) {

      trackingEnabled = false;

      motorHorizontal.stop();
      motorVertical.stop();

      Serial.println(
        "[MQTT CONTROL] Tracker BERHENTI dari dashboard"
      );

    } else {

      Serial.printf(
        "[MQTT CONTROL] Perintah tidak dikenal: %s\n",
        command
      );
    }
  }
}

// =====================================================
// 9. WIFI MANAGEMENT
// =====================================================
void startWiFi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);

  lastWiFiAttempt = millis();

  Serial.printf(
    "[WiFi] Mencoba terhubung ke %s\n",
    ssid
  );
}

void maintainWiFi() {

  if (WiFi.status() == WL_CONNECTED) {

    requestTimeSync();

    return;
  }

  // Reconnect tanpa delay agar tracking tidak terhenti.
  if (millis() - lastWiFiAttempt >= reconnectInterval) {

    lastWiFiAttempt = millis();

    Serial.println(
      "[WiFi] Koneksi terputus, mencoba kembali..."
    );

    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

// =====================================================
// 10. MQTT RECONNECT
// =====================================================
void reconnectMQTT() {

  if (
    client.connected() ||
    WiFi.status() != WL_CONNECTED
  ) {
    return;
  }

  unsigned long now = millis();

  if (now - lastMqttReconnect < reconnectInterval) {
    return;
  }

  lastMqttReconnect = now;

  String clientId =
    "HeliotropsESP32-" +
    String((uint32_t)esp_random(), HEX);

  Serial.print("[MQTT] Menghubungkan ke broker...");

  if (client.connect(clientId.c_str())) {

    Serial.println(" BERHASIL!");

    client.subscribe(TOPIC_CONTROL);

    Serial.println(
      "[MQTT] Subscribe control berhasil."
    );

  } else {

    Serial.printf(
      " GAGAL, rc=%d\n",
      client.state()
    );
  }
}

// =====================================================
// 11. SENSOR READING: LDR
// =====================================================
void readLDR() {

  topValue = analogRead(ldrTop);
  bottomValue = analogRead(ldrBottom);

  leftValue = analogRead(ldrLeft);
  rightValue = analogRead(ldrRight);

  horizontalDelta = leftValue - rightValue;

  verticalDelta = topValue - bottomValue;

  lightIntensity = (
    topValue +
    bottomValue +
    leftValue +
    rightValue
  ) / 4;
}

// =====================================================
// 12. SENSOR READING: INA219
// =====================================================
void readINA219() {

  if (!ina219Ready) {

    panelVoltage = 0;
    current_mA = 0;
    power_mW = 0;

    return;
  }

  busVoltage = ina219.getBusVoltage_V();

  shuntVoltage_mV = ina219.getShuntVoltage_mV();

  panelVoltage =
    busVoltage + (shuntVoltage_mV / 1000.0);

  current_mA = ina219.getCurrent_mA();

  // Validasi pembacaan sensor.
  if (
    !isfinite(panelVoltage) ||
    !isfinite(current_mA) ||
    panelVoltage < 0 ||
    panelVoltage > 32 ||
    fabs(current_mA) > 2000
  ) {

    Serial.println(
      "[INA219] Pembacaan tidak valid; nilai sensor diset 0."
    );

    panelVoltage = 0;
    current_mA = 0;
    power_mW = 0;

    return;
  }

  // V x mA = mW.
  power_mW = panelVoltage * current_mA;

  unsigned long now = millis();

  float elapsedHours =
    (now - previousEnergyTime) / 3600000.0;

  if (previousEnergyTime != 0 && power_mW > 0) {

    energy_Wh +=
      (power_mW / 1000.0) * elapsedHours;
  }

  previousEnergyTime = now;
}

// =====================================================
// 13. CREATE AND PUBLISH SENSOR DATA
// Data dikirim langsung melalui MQTT.
// Tidak menggunakan antrean offline.
// =====================================================
void publishSensorData() {

  if (!client.connected()) {

    Serial.println(
      "[MQTT] Offline. Data tidak dikirim."
    );

    return;
  }

  JsonDocument doc;

  String packetId = getPacketId();

  char timestamp[32];

  bool timestampValid = getMeasurementTimestamp(
    timestamp,
    sizeof(timestamp)
  );

  doc["packet_id"] = packetId;

  doc["timestamp"] = timestamp;

  doc["timestamp_valid"] = timestampValid;

  // Data LDR.
  doc["ldr_top"] = topValue;
  doc["ldr_bottom"] = bottomValue;
  doc["ldr_left"] = leftValue;
  doc["ldr_right"] = rightValue;

  doc["light_intensity"] = lightIntensity;

  // Data INA219.
  doc["voltage"] = panelVoltage;
  doc["bus_voltage"] = busVoltage;
  doc["shunt_voltage"] = shuntVoltage_mV;

  doc["current"] = current_mA;
  doc["power"] = power_mW;
  doc["energy"] = energy_Wh;

  // Posisi motor.
  long horizontalSteps =
    motorHorizontal.currentPosition() %
    STEPS_PER_REVOLUTION;

  if (horizontalSteps < 0) {
    horizontalSteps += STEPS_PER_REVOLUTION;
  }

  float azimuthAngle =
    horizontalSteps * 360.0 /
    STEPS_PER_REVOLUTION;

  float elevationAngle =
    motorVertical.currentPosition() * 360.0 /
    STEPS_PER_REVOLUTION;

  doc["azimuth"] = azimuthAngle;

  doc["elevation"] = elevationAngle;

  // Status tracking.
  doc["tracking_status"] =
    trackingEnabled ? "ON" : "OFF";

  // Serialisasi JSON.
  String packet;

  serializeJson(doc, packet);

  // Publish langsung ke MQTT.
  if (client.publish(TOPIC_SENSOR, packet.c_str())) {

    Serial.printf(
      "[MQTT] Data berhasil dikirim. ID: %s\n",
      packetId.c_str()
    );

  } else {

    Serial.println(
      "[MQTT] Gagal mengirim data."
    );
  }
}

// =====================================================
// 14. BUTTON CONTROL
// =====================================================
void checkButton() {

  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {

    lastDebounceTime = millis();
  }

  if (
    (millis() - lastDebounceTime) >
    debounceDelay
  ) {

    if (reading != stableButtonState) {

      stableButtonState = reading;

      if (stableButtonState == BUTTON_PRESSED) {

        trackingEnabled = !trackingEnabled;

        Serial.printf(
          "[BUTTON] TRACKING: %s\n",
          trackingEnabled ? "ON" : "OFF"
        );

        if (!trackingEnabled) {

          motorHorizontal.stop();
          motorVertical.stop();
        }
      }
    }
  }

  lastButtonReading = reading;
}

// =====================================================
// 15. SOLAR TRACKING ALGORITHM
// DIPERTAHANKAN
// =====================================================
void solarTracking() {

  if (!trackingEnabled) {
    return;
  }

  if (
    abs(horizontalDelta) > threshold &&
    motorHorizontal.distanceToGo() == 0
  ) {

    int directionH =
      horizontalDelta > 0 ? 1 : -1;

    motorHorizontal.move(
      directionH *
      correctionSteps *
      HORIZONTAL_DIRECTION
    );
  }

  if (
    abs(verticalDelta) > threshold &&
    motorVertical.distanceToGo() == 0
  ) {

    int directionV =
      verticalDelta > 0 ? 1 : -1;

    motorVertical.move(
      directionV *
      correctionSteps *
      VERTICAL_DIRECTION
    );
  }
}

// =====================================================
// 16. MOTOR CONTROL
// =====================================================
void runMotors() {

  motorHorizontal.run();

  motorVertical.run();
}

// =====================================================
// 17. LCD DISPLAY
// =====================================================
void updateLCD() {

  if (millis() - previousLCDTime < 500) {
    return;
  }

  previousLCDTime = millis();

  lcd.setCursor(0, 0);

  lcd.print("L:");

  lcd.print(lightIntensity);

  lcd.print(" T:");

  lcd.print(trackingEnabled ? "ON " : "OFF");

  lcd.print("   ");

  lcd.setCursor(0, 1);

  if (ina219Ready) {

    lcd.print("V:");

    lcd.print(panelVoltage, 2);

    lcd.print(" I:");

    lcd.print(current_mA, 1);

    lcd.print("mA ");

  } else {

    lcd.print("INA219 ERROR    ");
  }
}

// =====================================================
// 18. SERIAL MONITOR
// =====================================================
void printMonitoring() {

  Serial.println(
    "\n====== HELIOTROPS MONITORING ======"
  );

  Serial.printf(
    "LDR TOP/BOTTOM/LEFT/RIGHT: %d / %d / %d / %d\n",
    topValue,
    bottomValue,
    leftValue,
    rightValue
  );

  Serial.printf(
    "Light Intensity: %d\n",
    lightIntensity
  );

  Serial.printf(
    "Panel Voltage: %.3f V\n",
    panelVoltage
  );

  Serial.printf(
    "Current: %.3f mA\n",
    current_mA
  );

  Serial.printf(
    "Power: %.3f mW\n",
    power_mW
  );

  Serial.printf(
    "Energy: %.6f Wh\n",
    energy_Wh
  );

  Serial.printf(
    "Tracking Status: %s\n",
    trackingEnabled ? "ON" : "OFF"
  );

  Serial.printf(
    "WiFi: %s | MQTT: %s\n",
    WiFi.status() == WL_CONNECTED
      ? "CONNECTED"
      : "OFFLINE",
    client.connected()
      ? "CONNECTED"
      : "OFFLINE"
  );

  Serial.println(
    "==================================="
  );
}

// =====================================================
// 19. SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  analogReadResolution(12);

  analogSetPinAttenuation(ldrTop, ADC_11db);
  analogSetPinAttenuation(ldrBottom, ADC_11db);
  analogSetPinAttenuation(ldrLeft, ADC_11db);
  analogSetPinAttenuation(ldrRight, ADC_11db);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Motor configuration.
  motorHorizontal.setMaxSpeed(800);
  motorHorizontal.setAcceleration(400);

  motorVertical.setMaxSpeed(800);
  motorVertical.setAcceleration(400);

  // INA219 initialization.
  if (!ina219.begin()) {

    Serial.println(
      "[INA219] Tidak terdeteksi!"
    );

    ina219Ready = false;

  } else {

    ina219.setCalibration_32V_2A();

    ina219Ready = true;

    Serial.println(
      "[INA219] Berhasil terhubung."
    );
  }

  // LCD initialization.
  lcd.init();

  lcd.backlight();

  lcd.setCursor(0, 0);

  lcd.print("HELIOTROPS");

  lcd.setCursor(0, 1);

  lcd.print("System Starting");

  // MQTT configuration.
  client.setServer(
    mqtt_server,
    mqtt_port
  );

  client.setBufferSize(1024);

  client.setKeepAlive(30);

  client.setCallback(mqttCallback);

  // WiFi initialization.
  startWiFi();

  previousEnergyTime = millis();

  delay(1200);

  lcd.clear();

  Serial.println("=================================");
  Serial.println(" HELIOTROPS SYSTEM STARTED");
  Serial.println(" MQTT: DIRECT PUBLISH");
  Serial.println(" Offline queue: DISABLED");
  Serial.println("=================================");
}

// =====================================================
// 20. MAIN LOOP
// =====================================================
void loop() {

  maintainWiFi();

  if (WiFi.status() == WL_CONNECTED) {

    if (!client.connected()) {

      reconnectMQTT();
    }

    client.loop();
  }

  checkButton();

  runMotors();

  if (
    millis() - previousSensorTime >=
    sensorInterval
  ) {

    previousSensorTime = millis();

    readLDR();

    readINA219();

    solarTracking();

    printMonitoring();

    // Publish langsung tanpa antrean.
    publishSensorData();
  }

  updateLCD();
}
