#include <Arduino.h>
#include <Wire.h>
#include <MPU6050_light.h>
#include <Preferences.h>
#include <SimpleDHT.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ----------------- ขา -----------------
const int RELAY1_PIN = 18; // รีเลย์ท่านั่ง
const int RELAY2_PIN = 19; // รีเลย์ท่านอน
#define DHTPIN 16
#define MQ2PIN 34
#define BUZZER_PIN 17
#define BUZZER_CHANNEL 0

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
const unsigned long commandDebounce = 2500; // ms

// ---------- Function Sit and Lie ----------
unsigned long sitStartTime = 0;
unsigned long lieStartTime = 0;
bool sitActive = false;
bool lieActive = false;
bool sitLieLock = false;  // ล็อกเมื่อ Sit/Lie ทำงาน
const unsigned long relayDuration = 12000; // 12 วินาที

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

// ---------- BLE Callbacks ----------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { 
    deviceConnected = true; 
    Serial.println("BLE: connected"); 
  }
  void onDisconnect(BLEServer* pServer) { 
    deviceConnected = false; 
    Serial.println("BLE: disconnected"); 
    pServer->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;
    String command = String(value.c_str());
    Serial.print("BLE receive: "); Serial.println(command);

    unsigned long now = millis();

    // ---------------- ป้องกันคำสั่งอื่นระหว่าง Sit/Lie ----------------
    if (sitLieLock && command != "Sit" && command != "Lie") {
      sendStatus("อยู่ระหว่างปรับโซฟา");
      return;
    }

    // ---------------- AUTO / LOAD พรีเซต ----------------
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

    // ---------------- SAVE พรีเซต ----------------
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

// ---------- ฟังก์ชัน Preset ----------
void savePosition(int slot) {
  if (slot < 1 || slot > NUM_PRESETS) return;

  mpu1.update(); mpu2.update();
  targetRoll1 = atan2(-mpu1.getAccX(), mpu1.getAccZ())*180/PI;
  targetRoll2 = atan2(-mpu2.getAccX(), mpu2.getAccZ())*180/PI;
  lastRoll1 = targetRoll1; lastRoll2 = targetRoll2;
  hasTarget = true;

  prefs.begin("sofa", false);
  prefs.putFloat(("roll1_" + String(slot)).c_str(), targetRoll1);
  prefs.putFloat(("roll2_" + String(slot)).c_str(), targetRoll2);
  prefs.putFloat(("ofs1x_" + String(slot)).c_str(), mpu1.getAccXoffset());
  prefs.putFloat(("ofs1y_" + String(slot)).c_str(), mpu1.getAccYoffset());
  prefs.putFloat(("ofs1z_" + String(slot)).c_str(), mpu1.getAccZoffset());
  prefs.putFloat(("ofs2x_" + String(slot)).c_str(), mpu2.getAccXoffset());
  prefs.putFloat(("ofs2y_" + String(slot)).c_str(), mpu2.getAccYoffset());
  prefs.putFloat(("ofs2z_" + String(slot)).c_str(), mpu2.getAccZoffset());
  prefs.end();

  Serial.printf("บันทึกพรีเซต %d >> Roll1=%.2f | Roll2=%.2f\n", slot, targetRoll1, targetRoll2);
}

bool loadPosition(int slot) {
  if (slot < 1 || slot > NUM_PRESETS) return false;

  prefs.begin("sofa", false);
  targetRoll1 = prefs.getFloat(("roll1_" + String(slot)).c_str(), 0);
  targetRoll2 = prefs.getFloat(("roll2_" + String(slot)).c_str(), 0);
  mpu1.setAccOffsets(
    prefs.getFloat(("ofs1x_" + String(slot)).c_str(), 0),
    prefs.getFloat(("ofs1y_" + String(slot)).c_str(), 0),
    prefs.getFloat(("ofs1z_" + String(slot)).c_str(), 0)
  );
  mpu2.setAccOffsets(
    prefs.getFloat(("ofs2x_" + String(slot)).c_str(), 0),
    prefs.getFloat(("ofs2y_" + String(slot)).c_str(), 0),
    prefs.getFloat(("ofs2z_" + String(slot)).c_str(), 0)
  );
  prefs.end();

  hasTarget = true;
  Serial.printf("โหลดพรีเซต %d >> Roll1=%.2f | Roll2=%.2f\n", slot, targetRoll1, targetRoll2);
  return true;
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

  Serial.printf("Auto debug >> weightedError=%.2f | currentPosition=%d | switchState=%d\n",
                weightedError, currentPosition, switchState);

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

  // ส่งค่าเป็น comma-separated เพื่อ Flutter แยกแสดงใน _infoCard
  String sensorMsg = String((int)temperature) + "," + String((int)humidity) + "," + String(mq2Val);
  sendStatus(sensorMsg);

  // เตือน
  if (temperature > TEMP_THRESHOLD || mq2Val > MQ2_THRESHOLD) {
    toggleTone = true;
  } else {
    toggleTone = false;
    toneState = false;
    ledcWrite(BUZZER_CHANNEL, 0);
  }
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

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

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
  readSensors();
  buzzerControl();
}
