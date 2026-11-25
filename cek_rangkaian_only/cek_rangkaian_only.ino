#include <Wire.h>
#include <AccelStepper.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_I2C.h>

// ==========================================
// 1. HELIOSTOPS - PIN CONFIGURATION
// ==========================================

// LDR
const int ldrTop    = 34;
const int ldrBottom = 35;
const int ldrLeft   = 32;
const int ldrRight  = 33;

// MOTOR 1 - HORIZONTAL
#define M1_IN1 19
#define M1_IN2 18
#define M1_IN3 5
#define M1_IN4 17

// MOTOR 2 - VERTICAL
#define M2_IN1 23
#define M2_IN2 16
#define M2_IN3 4
#define M2_IN4 2

// Push Button
#define BUTTON_PIN 13

// I2C
#define SDA_PIN 21
#define SCL_PIN 22

// ==========================================
// 2. INITIALIZATION
// ==========================================

#define HALFSTEP 8
#define STEPS_PER_REVOLUTION 4096

// Urutan pin: IN1, IN3, IN2, IN4
AccelStepper motorHorizontal(
  HALFSTEP, M1_IN1, M1_IN3, M1_IN2, M1_IN4
);

AccelStepper motorVertical(
  HALFSTEP, M2_IN1, M2_IN3, M2_IN2, M2_IN4
);

// INA219
Adafruit_INA219 ina219;

// LCD 16x2
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================================
// 3. TRACKING CONFIGURATION
// ==========================================

bool trackingEnabled = false;

// Threshold perbedaan nilai LDR
const int threshold = 250;

// Jumlah langkah koreksi setiap pergerakan (bisa dinaikkan jika ingin lebih ngebut, misal 24 atau 32)
const int correctionSteps = 16;

// Interval pembacaan sensor
const unsigned long sensorInterval = 200;

const int HORIZONTAL_DIRECTION = 1;
const int VERTICAL_DIRECTION = 1;

float shuntVoltage_mV = 0;
float panelVoltage = 0;

// ==========================================
// 4. BUTTON CONFIGURATION
// ==========================================

const int BUTTON_PRESSED = LOW;

int lastButtonReading = HIGH;
int stableButtonState = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ==========================================
// 5. SENSOR VARIABLES
// ==========================================

int topValue = 0;
int bottomValue = 0;
int leftValue = 0;
int rightValue = 0;

int horizontalDelta = 0;
int verticalDelta = 0;

int lightIntensity = 0;

float busVoltage = 0;
float current_mA = 0;
float power_mW = 0;

unsigned long previousSensorTime = 0;
unsigned long previousLCDTime = 0;

// ==========================================
// 6. SETUP
// ==========================================

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  analogReadResolution(12);

  analogSetPinAttenuation(ldrTop, ADC_11db);
  analogSetPinAttenuation(ldrBottom, ADC_11db);
  analogSetPinAttenuation(ldrLeft, ADC_11db);
  analogSetPinAttenuation(ldrRight, ADC_11db);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Kecepatan motor ditingkatkan agar lebih cepat (MaxSpeed & Acceleration)
  motorHorizontal.setMaxSpeed(800);
  motorHorizontal.setAcceleration(400);

  motorVertical.setMaxSpeed(800);
  motorVertical.setAcceleration(400);

  if (!ina219.begin()) {
    Serial.println("INA219 tidak terdeteksi!");
  } else {
    ina219.setCalibration_32V_2A();
    Serial.println("INA219 berhasil terhubung!");
  }

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("HELIOSTOPS");
  lcd.setCursor(0, 1);
  lcd.print("System Starting");

  delay(2000);
  lcd.clear();

  Serial.println("==============================");
  Serial.println(" HELIOSTOPS SYSTEM STARTED");
  Serial.println("==============================");
}

// ==========================================
// 7. READ LDR SENSORS
// ==========================================

void readLDR() {
  topValue = analogRead(ldrTop);
  bottomValue = analogRead(ldrBottom);
  leftValue = analogRead(ldrLeft);
  rightValue = analogRead(ldrRight);

  horizontalDelta = leftValue - rightValue;
  verticalDelta = topValue - bottomValue;

  lightIntensity = (topValue + bottomValue + leftValue + rightValue) / 4;
}

// ==========================================
// 8. READ INA219
// ==========================================


void readINA219() {
  busVoltage = ina219.getBusVoltage_V();

  shuntVoltage_mV = ina219.getShuntVoltage_mV();

  // Menghitung tegangan pada sisi VIN+
  panelVoltage = busVoltage + (shuntVoltage_mV / 1000.0);

  // Membaca arus
  current_mA = ina219.getCurrent_mA();

  power_mW = panelVoltage * current_mA;
}


// ==========================================
// 9. BUTTON CONTROL
// ==========================================

void checkButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == BUTTON_PRESSED) {
        trackingEnabled = !trackingEnabled;

        Serial.print("TRACKING: ");
        Serial.println(trackingEnabled ? "ON" : "OFF");

        if (!trackingEnabled) {
          motorHorizontal.stop();
          motorVertical.stop();
        }
      }
    }
  }

  lastButtonReading = reading;
}

// ==========================================
// 10. SOLAR TRACKING ALGORITHM (2 JALUR TERPISAH)
// ==========================================

void solarTracking() {
  if (!trackingEnabled) {
    return;
  }

  // --- JALUR 1: KONTROL MOTOR HORIZONTAL (Kanan - Kiri) ---
  if (abs(horizontalDelta) > threshold && motorHorizontal.distanceToGo() == 0) {
    int directionH = horizontalDelta > 0 ? 1 : -1;
    motorHorizontal.move(directionH * correctionSteps * HORIZONTAL_DIRECTION);
  }

  // --- JALUR 2: KONTROL MOTOR VERTIKAL (Atas - Bawah) ---
  // Dipisah independen agar tidak saling menunggu dengan horizontal
  if (abs(verticalDelta) > threshold && motorVertical.distanceToGo() == 0) {
    int directionV = verticalDelta > 0 ? 1 : -1;
    motorVertical.move(directionV * correctionSteps * VERTICAL_DIRECTION);
  }
}

// ==========================================
// 11. MOTOR EXECUTION (Dijalankan terus-menerus)
// ==========================================

void runMotors() {
  motorHorizontal.run();
  motorVertical.run();
}

// ==========================================
// 12. LCD DISPLAY
// ==========================================

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

  lcd.setCursor(0, 1);
  lcd.print("V:");
  lcd.print(busVoltage, 2);
  lcd.print(" I:");
  lcd.print(current_mA, 1);
  lcd.print("mA ");
}

// ==========================================
// 13. SERIAL MONITOR
// ==========================================

void printMonitoring() {
  Serial.println("\n====== HELIOSTOPS MONITORING ======");
  Serial.print("LDR TOP    : "); Serial.println(topValue);
  Serial.print("LDR BOTTOM : "); Serial.println(bottomValue);
  Serial.print("LDR LEFT   : "); Serial.println(leftValue);
  Serial.print("LDR RIGHT  : "); Serial.println(rightValue);
  Serial.print("Horizontal Delta: "); Serial.println(horizontalDelta);
  Serial.print("Vertical Delta: "); Serial.println(verticalDelta);
  Serial.print("Light Intensity: "); Serial.println(lightIntensity);
  Serial.println("-----------------------------------");
  Serial.print("Voltage : "); Serial.print(busVoltage); Serial.println(" V");
  Serial.print("Current : "); Serial.print(current_mA); Serial.println(" mA");
  Serial.print("Power   : "); Serial.print(power_mW); Serial.println(" mW");
  Serial.println("-----------------------------------");
  Serial.print("Tracking Status: "); Serial.println(trackingEnabled ? "ON" : "OFF");
  Serial.println("===================================");
  Serial.print("Panel Voltage : ");
  Serial.print(panelVoltage, 3);
  Serial.println(" V");

  Serial.print("Bus Voltage   : ");
  Serial.print(busVoltage, 3);
  Serial.println(" V");

  Serial.print("Shunt Voltage : ");
  Serial.print(shuntVoltage_mV, 3);
  Serial.println(" mV");

  Serial.print("Current       : ");
  Serial.print(current_mA, 3);
  Serial.println(" mA");
}

// ==========================================
// 14. MAIN LOOP
// ==========================================

void loop() {
  // Tombol wajib diperiksa paling utama
  checkButton();

  // Kedua motor diproses secara konstan agar bisa bergerak bersamaan (paralel)
  runMotors();

  // Pembacaan sensor berkala
  if (millis() - previousSensorTime >= sensorInterval) {
    previousSensorTime = millis();

    readLDR();
    readINA219();
    solarTracking(); // Mengevaluasi apakah sumbu X atau Y perlu bergerak
    
    printMonitoring(); // Komentar ini bisa diaktifkan jika ingin lihat serial monitor, tapi dinonaktifkan sementara agar proses makin ngebut
  }

  updateLCD();
}