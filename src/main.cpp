#include <Arduino.h>
#include <DHT.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// กำหนดขารีเลย์และเซนเซอร์
const int relay1Pin = 22;
const int relay2Pin = 23;
#define DHTPIN 16
#define DHTTYPE DHT22
#define MQ2PIN 34
#define BUZZER_PIN 21
#define SWITCH1 19
#define SWITCH2 18
#define SWITCH3 5
#define SWITCH4 17

DHT dht(DHTPIN, DHTTYPE);
bool systemActive = false;

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic;
BLECharacteristic *sensorCharacteristic;
bool deviceConnected = false;
unsigned long lastDHTReadTime = 0;
const long DHTReadInterval = 2000;

#define TEMP_THRESHOLD 60.0
#define MQ2_THRESHOLD 2000
#define SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_UUID "abcd1234-5678-1234-5678-abcdef123456"
#define SENSOR_CHARACTERISTIC_UUID "1234abcd-5678-1234-5678-abcdef654321"

// ฟังก์ชันเช็คว่ากดครบ 4 ปุ่มหรือไม่
bool isAllSwitchPressed() {
    return digitalRead(SWITCH1) == LOW &&
           digitalRead(SWITCH2) == LOW &&
           digitalRead(SWITCH3) == LOW &&
           digitalRead(SWITCH4) == LOW;
}

// ฟังก์ชันเช็คว่าปล่อยครบ 4 ปุ่มหรือไม่
bool isAllSwitchReleased() {
    return digitalRead(SWITCH1) == HIGH &&
           digitalRead(SWITCH2) == HIGH &&
           digitalRead(SWITCH3) == HIGH &&
           digitalRead(SWITCH4) == HIGH;
}

// BLE Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        pServer->startAdvertising();
    }
};

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String command = String(value.c_str());
            Serial.print("📩 คำสั่งที่ได้รับ: ");
            Serial.println(command);

            if (command == "ON1") {
                digitalWrite(relay1Pin, HIGH);
                Serial.println("✅ รีเลย์ 1 เปิด");
            } else if (command == "OFF1") {
                digitalWrite(relay1Pin, LOW);
                Serial.println("✅ รีเลย์ 1 ปิด");
            } else if (command == "ON2") {
                digitalWrite(relay2Pin, HIGH);
                Serial.println("✅ รีเลย์ 2 เปิด");
            } else if (command == "OFF2") {
                digitalWrite(relay2Pin, LOW);
                Serial.println("✅ รีเลย์ 2 ปิด");
            }
        }
    }
};

void setup() {
    Serial.begin(9600);

    pinMode(relay1Pin, OUTPUT_OPEN_DRAIN);
    pinMode(relay2Pin, OUTPUT_OPEN_DRAIN);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(SWITCH1, INPUT_PULLUP);
    pinMode(SWITCH2, INPUT_PULLUP);
    pinMode(SWITCH3, INPUT_PULLUP);
    pinMode(SWITCH4, INPUT_PULLUP);

    digitalWrite(relay1Pin, LOW);
    digitalWrite(relay2Pin, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    dht.begin();
}

void startSystem() {
    systemActive = true;
    Serial.println("✅ ระบบเริ่มทำงาน!");

    BLEDevice::init("ESP32_BLE_Sofa");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_WRITE
                    );
    pCharacteristic->setCallbacks(new MyCallbacks());

    sensorCharacteristic = pService->createCharacteristic(
                           SENSOR_CHARACTERISTIC_UUID,
                           BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                         );
    sensorCharacteristic->addDescriptor(new BLE2902());

    pService->start();
    pServer->getAdvertising()->start();
    Serial.println("✅ BLE พร้อมใช้งาน");
}

void stopSystem() {
    Serial.println("🛑 ระบบหยุดทำงาน!");
    systemActive = false;
    
    BLEDevice::deinit();
    digitalWrite(relay1Pin, LOW);
    digitalWrite(relay2Pin, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
    // ถ้ายังไม่ได้เปิดระบบ และกดครบ 4 ปุ่ม -> เริ่มระบบ
    if (!systemActive && isAllSwitchPressed()) {
        startSystem();
    }

    // ถ้าระบบทำงานอยู่ และปล่อยครบ 4 ปุ่ม -> หยุดระบบ
    if (systemActive && isAllSwitchReleased()) {
        stopSystem();
    }

    // ถ้าระบบยังไม่ทำงาน ไม่ต้องทำอะไรต่อ
    if (!systemActive) return;

    // อ่านค่าจากเซนเซอร์ทุกๆ 2 วินาที
    if (millis() - lastDHTReadTime >= DHTReadInterval) {
        lastDHTReadTime = millis();
        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        int mq2_value = analogRead(MQ2PIN);

        if (!isnan(temperature) && !isnan(humidity)) {
            String sensorData = String(temperature) + "," + String(humidity) + "," + String(mq2_value);
            sensorCharacteristic->setValue(sensorData.c_str());
            sensorCharacteristic->notify();
            Serial.println("📡 ส่งข้อมูล: " + sensorData);

            // แจ้งเตือนถ้าเกินค่าที่กำหนด
            if (temperature > TEMP_THRESHOLD || mq2_value > MQ2_THRESHOLD) {
                digitalWrite(BUZZER_PIN, HIGH);
                Serial.println("❗เตือน! อุณหภูมิหรือค่าก๊าซเกินเกณฑ์!");
            } else {
                digitalWrite(BUZZER_PIN, LOW);
            }
        } else {
            Serial.println("❌ ไม่สามารถอ่านค่า DHT22 ได้!");
        }
    }
}
