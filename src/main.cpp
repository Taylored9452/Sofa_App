#include <Arduino.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <Preferences.h>
#include <SimpleDHT.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_sleep.h"

// ----------------- ขา ------------------
const int RELAY1_PIN = 18;
const int RELAY2_PIN = 19;
#define DHTPIN 16
#define MQ2PIN 34
#define BUZZER_PIN 17
#define BUZZER_CHANNEL 0
#define FORCE_SENSOR_PIN 32     

// ---------- Force sensor + Deep Sleep ----------
#define SLEEP_THRESHOLD 2500     // ค่า ADC ต่ำกว่าถือว่าไม่มีคนนั่ง
#define SLEEP_HYSTERESIS 500
#define DEEP_SLEEP_DELAY 15000    // ms
unsigned long lastBelowThreshold = 0;

// ---------- MPU6050 ----------
TwoWire I2C_1 = TwoWire(0);
TwoWire I2C_2 = TwoWire(1);
MPU6050 mpu1(I2C_1);
MPU6050 mpu2(I2C_2);
Preferences prefs;
unsigned long lastMPUReadTime = 0;
const unsigned long mpuReadInterval = 200;
float targetRoll1 = 0, targetRoll2 = 0;
float lastRoll1 = 0, lastRoll2 = 0;
bool hasTarget = false;
bool autoActive = false;
bool mpuAvailable = false;
unsigned long lastCheckMPU = 0;

// ---------- Auto Mode ----------
enum Position {SITTING, LYING};
enum SwitchState {IDLE, SWITCH_WAIT};
Position currentPosition = SITTING;
SwitchState switchState = IDLE;
unsigned long lastSwitchTime = 0;

// ---------- BLE ----------
BLEServer *pServer = NULL;
BLECharacteristic *cmdCharacteristic;
BLECharacteristic *sensorCharacteristic;
bool deviceConnected = false;
String currentClientAddress = "";

// ---------- Sensor ----------
SimpleDHT22 dht22;
bool toggleTone = false;
bool toneState = false;
unsigned long lastSensorReadTime = 0;
const long sensorReadInterval = 2000;
unsigned long lastToneToggleTime = 0;
const long toneToggleInterval = 200;
#define TEMP_THRESHOLD 60.0
#define MQ2_THRESHOLD 2000

// ---------- BLE UUIDs ----------
#define SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcd1234-5678-1234-5678-abcdef123456"
#define SENSOR_CHARACTERISTIC_UUID "1234abcd-5678-1234-5678-abcdef654321"

// ---------- Debounce ----------
unsigned long lastAutoCommandTime = 0;
unsigned long lastSaveCommandTime = 0;
const unsigned long commandDebounce = 2500;

// ---------- Function Sit and Lie ----------
unsigned long sitStartTime = 0;
unsigned long lieStartTime = 0;
bool sitActive = false;
bool lieActive = false;
bool sitLieLock = false;
const unsigned long relayDuration = 12000;

// ---------- Presets ----------
const int NUM_PRESETS = 3;

// ---------- Function prototypes ----------
void checkMPU();
void savePosition(int slot);
bool loadPosition(int slot);
void autoControl(unsigned long now);
void handleRelaySwitch(unsigned long now);
void readSensors();
void buzzerControl();
void checkRelayTimer(unsigned long now);
void setupBLE();
void sendStatus(String status);
void checkForceSensorAndSleep();   // Deep sleep

// ---------- BLE Callbacks ----------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
    deviceConnected = true;
    char address[18];
    sprintf(address, "%02X:%02X:%02X:%02X:%02X:%02X",
      param->connect.remote_bda[0],
      param->connect.remote_bda[1],
      param->connect.remote_bda[2],
      param->connect.remote_bda[3],
      param->connect.remote_bda[4],
      param->connect.remote_bda[5]
    );
    currentClientAddress = String(address);
    Serial.print("📱 BLE Connected from: ");
    Serial.println(currentClientAddress);
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("📴 BLE disconnected");
    currentClientAddress = "";
    pServer->startAdvertising();
  }
};

// ---------- BLE Command ----------
class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;
    String command = String(value.c_str());
    Serial.print("BLE receive: "); Serial.println(command);

    unsigned long now = millis();

    if (sitLieLock && command != "Sit" && command != "Lie") {
      sendStatus("อยู่ระหว่างปรับโซฟา");
      return;
    }

    // ---------------- AUTO / LOAD ----------------
    if (command.startsWith("AUTO")) {
      int slot = 1;
      if (command.length() > 4) slot = command[4] - '0';
      if (slot < 1 || slot > NUM_PRESETS) slot = 1;

      if (loadPosition(slot)) {
        if (hasTarget) {
          mpu1.update(); delay(5); mpu2.update();
          float roll1 = atan2(-mpu1.getAccX(), mpu1.getAccZ()) * 180.0 / PI;
          float roll2 = atan2(-mpu2.getAccX(), mpu2.getAccZ()) * 180.0 / PI;
          float currentWeighted = (targetRoll1 - roll1) * 0.7 + (targetRoll2 - roll2) * 0.3;
          const float tol = 5.0;
          if (fabs(currentWeighted) <= tol) {
            sendStatus("Auto Mode: ถึงตำแหน่งแล้ว!");
            autoActive = false;
          } else {
            currentPosition = (currentWeighted > 0) ? LYING : SITTING;
            switchState = IDLE;
            autoActive = true;
            sendStatus("Auto Mode: เริ่มทำงาน");
          }
        }
      } else {
        sendStatus("ไม่มีพรีเซต " + String(slot));
      }
      lastAutoCommandTime = now;
      return;
    }

    // ---------------- SAVE ----------------
    if (command.startsWith("SAVE")) {
      int slot = 1;
      if (command.length() > 4) slot = command[4] - '0';
      if (slot < 1 || slot > NUM_PRESETS) slot = 1;

      if (now - lastSaveCommandTime >= commandDebounce) {
        savePosition(slot);
        lastSaveCommandTime = now;
        sendStatus("บันทึกพรีเซต " + String(slot) + " เรียบร้อย");
      }
      return;
    }

    // ---------------- รีเลย์ ----------------
    if (command == "ON1") { if (!autoActive) digitalWrite(RELAY1_PIN, HIGH); else sendStatus("Auto Mode: กำลังทำงาน"); }
    else if (command == "OFF1") { if (!autoActive) digitalWrite(RELAY1_PIN, LOW); else sendStatus("Auto Mode: กำลังทำงาน"); }
    else if (command == "ON2") { if (!autoActive) digitalWrite(RELAY2_PIN, HIGH); else sendStatus("Auto Mode: กำลังทำงาน"); }
    else if (command == "OFF2") { if (!autoActive) digitalWrite(RELAY2_PIN, LOW); else sendStatus("Auto Mode: กำลังทำงาน"); }

    // ---------------- SIT ----------------
    else if (command == "Sit") {
      if (!autoActive && !sitActive && !lieActive) {
        digitalWrite(RELAY1_PIN, HIGH);
        sitStartTime = millis();
        sitActive = true;
        sitLieLock = true;
        sendStatus("SIT: ปรับท่านั่ง");
      } else sendStatus("ไม่สามารถปรับท่านั่งได้");
    }

    // ---------------- LIE ----------------
    else if (command == "Lie") {
      if (!autoActive && !sitActive && !lieActive) {
        digitalWrite(RELAY2_PIN, HIGH);
        lieStartTime = millis();
        lieActive = true;
        sitLieLock = true;
        sendStatus("LIE: ปรับท่านอน");
      } else sendStatus("ไม่สามารถปรับท่านอนได้");
    }
  }
};

// ---------- ส่งสถานะไป Flutter ----------
void sendStatus(String status) {
  if (deviceConnected && sensorCharacteristic != nullptr) {
    std::string utf8Status = std::string(status.c_str());
    sensorCharacteristic->setValue((uint8_t*)utf8Status.data(), utf8Status.length());
    sensorCharacteristic->notify();
    Serial.println("Notify status: " + status);
  }
}

// ---------- Setup BLE ----------
void setupBLE() {
  BLEDevice::init("ESP32_BLE_Sofa2");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  cmdCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  cmdCharacteristic->setCallbacks(new CommandCallbacks());

  sensorCharacteristic = pService->createCharacteristic(
    SENSOR_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  sensorCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE พร้อม - กำลังโฆษณา");
}

// 🔹 hash BLE address
String hashAddress(String addr) {
  uint32_t hash = 0;
  for (int i = 0; i < addr.length(); i++) hash = (hash*31) + addr[i];
  return String(hash, HEX);
}

// ---------- Preset ----------
void savePosition(int slot) {
  if (slot < 1 || slot > NUM_PRESETS) return;
  if (currentClientAddress == "") currentClientAddress = "unknown";
  String addrKey = hashAddress(currentClientAddress);

  mpu1.update(); mpu2.update();
  targetRoll1 = atan2(-mpu1.getAccX(), mpu1.getAccZ())*180/PI;
  targetRoll2 = atan2(-mpu2.getAccX(), mpu2.getAccZ())*180/PI;
  lastRoll1 = targetRoll1; lastRoll2 = targetRoll2;
  hasTarget = true;

  prefs.begin("sofa", false);
  String prefix = addrKey + "_";
  prefs.putFloat((prefix + "r1_" + slot).c_str(), targetRoll1);
  prefs.putFloat((prefix + "r2_" + slot).c_str(), targetRoll2);
  prefs.end();

  Serial.printf("บันทึกพรีเซต %d สำหรับ [%s] (key:%s) >> Roll1=%.2f | Roll2=%.2f\n",
                slot, currentClientAddress.c_str(), addrKey.c_str(), targetRoll1, targetRoll2);
}

bool loadPosition(int slot) {
  if (slot < 1 || slot > NUM_PRESETS) return false;
  if (currentClientAddress == "") currentClientAddress = "unknown";
  String addrKey = hashAddress(currentClientAddress);

  prefs.begin("sofa", false);
  String prefix = addrKey + "_";
  targetRoll1 = prefs.getFloat((prefix + "r1_" + slot).c_str(), 0);
  targetRoll2 = prefs.getFloat((prefix + "r2_" + slot).c_str(), 0);
  prefs.end();

  hasTarget = true;
  Serial.printf("โหลดพรีเซต %d สำหรับ [%s] (key:%s) >> Roll1=%.2f | Roll2=%.2f\n",
                slot, currentClientAddress.c_str(), addrKey.c_str(), targetRoll1, targetRoll2);

  return (targetRoll1 != 0 || targetRoll2 != 0);
}

// ---------- MPU ----------
void checkMPU() {
  unsigned long now = millis();
  if (now - lastCheckMPU > 1000) {
    lastCheckMPU = now;
    bool mpu1Ready = (mpu1.begin(0x68) == 0);
    bool mpu2Ready = (mpu2.begin(0x69) == 0);
    mpuAvailable = mpu1Ready && mpu2Ready;

    if (!mpuAvailable && autoActive) {
      autoActive = false;
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, LOW);
      sendStatus("MPU หลุด! หยุดโหมดอัตโนมัติ");
      Serial.println("MPU หลุด! หยุดโหมดอัตโนมัติ");
    }
  }
}

void autoControl(unsigned long now) {
  if (!autoActive || !mpuAvailable) return;
  if (now - lastMPUReadTime >= mpuReadInterval) {
    lastMPUReadTime = now;
    mpu1.update(); delay(5); mpu2.update();
    lastRoll1 = atan2(-mpu1.getAccX(), mpu1.getAccZ())*180/PI;
    lastRoll2 = atan2(-mpu2.getAccX(), mpu2.getAccZ())*180/PI;
    handleRelaySwitch(now);
  }
}

void handleRelaySwitch(unsigned long now) {
  const float tolerance = 5.0, hysteresis = 3.0;
  const unsigned long switchDelay = 1000;
  float weightedError = (targetRoll1 - lastRoll1)*0.7 + (targetRoll2 - lastRoll2)*0.3;

  switch (switchState) {
    case IDLE:
      if (currentPosition == SITTING && weightedError < -tolerance-hysteresis) {
        digitalWrite(RELAY1_PIN, LOW);
        lastSwitchTime = now;
        switchState = SWITCH_WAIT;
      } else if (currentPosition == LYING && weightedError > tolerance+hysteresis) {
        digitalWrite(RELAY2_PIN, LOW);
        lastSwitchTime = now;
        switchState = SWITCH_WAIT;
      } else if (fabs(weightedError) <= tolerance) {
        digitalWrite(RELAY1_PIN, LOW);
        digitalWrite(RELAY2_PIN, LOW);
        autoActive = false;
        sendStatus("Auto Mode: ถึงตำแหน่งแล้ว!");
      }
      break;
    case SWITCH_WAIT:
      if (now - lastSwitchTime >= switchDelay) {
        if (currentPosition == SITTING) {
          digitalWrite(RELAY2_PIN, HIGH);
          currentPosition = LYING;
        } else {
          digitalWrite(RELAY1_PIN, HIGH);
          currentPosition = SITTING;
        }
        switchState = IDLE;
      }
      break;
  }
}

// ---------- Relay Timer ----------
void checkRelayTimer(unsigned long now) {
  if (sitActive && now - sitStartTime >= relayDuration) {
    digitalWrite(RELAY1_PIN, LOW);
    sitActive = false;
    sitLieLock = false;
    sendStatus("SIT: ถึงต่ำแหน่งท่านั่ง");
  }
  if (lieActive && now - lieStartTime >= relayDuration) {
    digitalWrite(RELAY2_PIN, LOW);
    lieActive = false;
    sitLieLock = false;
    sendStatus("LIE: ถึงต่ำแหน่งท่านอน");
  }
}

// ---------- Sensor ----------
void readSensors() {
  unsigned long now = millis();
  if (now - lastSensorReadTime < sensorReadInterval) return;
  lastSensorReadTime = now;

  int err = SimpleDHTErrSuccess;
  float temperature = 0, humidity = 0;
  if ((err = dht22.read2(DHTPIN, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.println("DHT22 error");
  }

  int mq2Val = analogRead(MQ2PIN);
  String sensorMsg = String((int)temperature) + "," + String((int)humidity) + "," + String(mq2Val);
  sendStatus(sensorMsg);

  if (temperature > TEMP_THRESHOLD || mq2Val > MQ2_THRESHOLD) toggleTone = true;
  else { toggleTone = false; toneState = false; ledcWrite(BUZZER_CHANNEL, 0); }
}

// ---------- Buzzer ----------
void buzzerControl() {
  if (!toggleTone) return;
  unsigned long now = millis();
  if (now - lastToneToggleTime >= toneToggleInterval) {
    lastToneToggleTime = now;
    toneState = !toneState;
    ledcWrite(BUZZER_CHANNEL, toneState ? 150 : 0);
  }
}

// ---------- Force sensor + deep sleep ----------
void checkForceSensorAndSleep() {
  int val = analogRead(FORCE_SENSOR_PIN);

  if(val < SLEEP_HYSTERESIS) {
    if(lastBelowThreshold == 0) lastBelowThreshold = millis();
    else if(millis() - lastBelowThreshold >= DEEP_SLEEP_DELAY) {
      Serial.println("ไม่มีคนนั่ง → Deep Sleep");
      esp_sleep_enable_ext0_wakeup((gpio_num_t)FORCE_SENSOR_PIN, 1);
      esp_deep_sleep_start();
    }
  } else lastBelowThreshold = 0;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  pinMode(FORCE_SENSOR_PIN, INPUT);

  I2C_1.begin(21, 22);
  I2C_2.begin(25, 26);

  checkMPU();
  setupBLE();
}

// ---------- Loop ----------
void loop() {
  unsigned long now = millis();
  checkMPU();
  autoControl(now);
  checkRelayTimer(now);
  checkForceSensorAndSleep();  // ตรวจจับคนนั่ง → deep sleep
  readSensors();
  buzzerControl();
}
