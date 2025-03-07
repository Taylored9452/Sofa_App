#include <Arduino.h>
#include <DHT.h>
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

// กำหนดขารีเลย์
const int relay1Pin = 22;
const int relay2Pin = 23;

// กำหนดขาเซนเซอร์
#define DHTPIN 16       // ขาที่เชื่อม DHT22
#define DHTTYPE DHT22   // ใช้เซนเซอร์ DHT22
#define MQ2PIN 34       // ขาอนาล็อกของ MQ2

DHT dht(DHTPIN, DHTTYPE);

// ใช้ millis() แทน delay()
unsigned long lastDHTReadTime = 0;
const long DHTReadInterval = 2000;  // อ่านค่าทุก 2 วินาที

void setup() {
  pinMode(relay1Pin, OUTPUT_OPEN_DRAIN);
  pinMode(relay2Pin, OUTPUT_OPEN_DRAIN);

  digitalWrite(relay1Pin, LOW);
  digitalWrite(relay2Pin, LOW);

  Serial.begin(9600);
  SerialBT.begin("ESP32_BT_MAK"); // ตั้งชื่อ Bluetooth

  dht.begin();
  Serial.println("✅ ESP32 พร้อมใช้งาน");
}

void loop() {
  // ✅ อ่านค่าจาก Bluetooth
  static String command = "";
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n') {
      command.trim();
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

      command = ""; // รีเซ็ตตัวแปร
    } else {
      command += c;
    }
  }

  // ✅ อ่านค่า DHT22 และ MQ2 ทุก 2 วินาที
  if (millis() - lastDHTReadTime >= DHTReadInterval) {
    lastDHTReadTime = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int mq2_value = analogRead(MQ2PIN);

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("❌ ไม่สามารถอ่านค่า DHT22 ได้!");
    } else {
      Serial.print("🌡️ อุณหภูมิ: ");
      Serial.print(temperature);
      Serial.print("°C, 💧 ความชื้น: ");
      Serial.print(humidity);
      Serial.print("%, 🛢️ MQ2: ");
      Serial.println(mq2_value);
    }
  }
}
