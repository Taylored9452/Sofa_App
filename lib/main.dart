import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

void main() {
  runApp(MyApp());
}

class MyApp extends StatefulWidget {
  @override
  _MyAppState createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  FlutterBluePlus flutterBlue = FlutterBluePlus();
  BluetoothDevice? esp32Device;
  BluetoothCharacteristic? commandCharacteristic;
  BluetoothCharacteristic? sensorCharacteristic;
  bool isConnected = false;
  String temperature = "กำลังอ่านอุณหภูมิ...";
  String humidity = "กำลังอ่านความชื้น...";
  String mq2Value = "กำลังอ่านค่า MQ2...";

  bool relay1Status = false;
  bool relay2Status = false;

  final String SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
  final String CHARACTERISTIC_UUID = "abcd1234-5678-1234-5678-abcdef123456";
  final String SENSOR_CHARACTERISTIC_UUID = "1234abcd-5678-1234-5678-abcdef654321";

  @override
  void initState() {
    super.initState();
    scanDevices();
  }

  void scanDevices() async {
    try {
      await FlutterBluePlus.startScan(timeout: Duration(seconds: 5));
      FlutterBluePlus.scanResults.listen((results) {
        for (ScanResult r in results) {
          if (r.device.name == "ESP32_BLE_Sofa") {
            FlutterBluePlus.stopScan();
            connectToDevice(r.device);
            return;
          }
        }
        setState(() {
          temperature = "ไม่พบอุปกรณ์ ESP32";
          humidity = "ไม่พบอุปกรณ์ ESP32";
          mq2Value = "ไม่พบอุปกรณ์ ESP32";
        });
      });
    } catch (e) {
      setState(() {
        temperature = "เกิดข้อผิดพลาดขณะสแกน: $e";
      });
    }
  }

  void connectToDevice(BluetoothDevice device) async {
    try {
      await device.connect(timeout: Duration(seconds: 10));
      List<BluetoothService> services = await device.discoverServices();
      for (BluetoothService service in services) {
        if (service.uuid.toString() == SERVICE_UUID) {
          for (BluetoothCharacteristic characteristic in service.characteristics) {
            if (characteristic.uuid.toString() == CHARACTERISTIC_UUID) {
              commandCharacteristic = characteristic;
            } else if (characteristic.uuid.toString() == SENSOR_CHARACTERISTIC_UUID) {
              sensorCharacteristic = characteristic;
              if (sensorCharacteristic != null) {
                await sensorCharacteristic!.setNotifyValue(true);
                sensorCharacteristic!.value.listen((value) {
                  setState(() {
                    List<String> sensorValues = String.fromCharCodes(value).split(',');
                    if (sensorValues.length == 3) {
                      temperature = sensorValues[0];
                      humidity = sensorValues[1];
                      mq2Value = sensorValues[2];
                    }
                  });
                });
              }
            }
          }
        }
      }
      setState(() {
        esp32Device = device;
        isConnected = true;
      });
    } catch (e) {
      setState(() {
        temperature = "เชื่อมต่อไม่สำเร็จ: $e";
        isConnected = false;
      });
    }
  }

  void sendCommand(String command) async {
    if (commandCharacteristic != null && isConnected) {
      await commandCharacteristic!.write(command.codeUnits);
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        backgroundColor: Colors.grey[300],
        appBar: AppBar(
          title: Text(
            "Recliner Sofa",
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 34, color: Colors.white),
          ),
          backgroundColor: Colors.blue,
          elevation: 4,
          centerTitle: true,
        ),
        body: Column(
          children: [
            Padding(
              padding: const EdgeInsets.all(16.0),
              child: Row(
                children: [
                  Icon(Icons.home, size: 62, color: Colors.blue),
                  SizedBox(width: 10),
                  Expanded(
                    child: Container(
                      padding: EdgeInsets.symmetric(vertical: 12, horizontal: 16),
                      decoration: BoxDecoration(
                        color: Colors.white,
                        borderRadius: BorderRadius.circular(10),
                        border: Border.all(color: Colors.grey),
                      ),
                      child: Row(
                        children: [
                          Icon(Icons.cloud, color: Colors.blue),
                          SizedBox(width: 10),
                          Text("Info:  ", style: TextStyle(color: Colors.black, fontSize: 18)),
                          Text(
                            isConnected ? "เชื่อมต่อแล้ว" : "ไม่ได้เชื่อมต่อ",
                            style: TextStyle(color: Colors.black, fontSize: 18),
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
            SizedBox(height: 20),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                _infoCard("ppm", mq2Value, Icons.cloud),
                _infoCard("Temp", temperature, Icons.local_fire_department, color: Colors.red),
              ],
            ),
            SizedBox(height: 30),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _circleButton(Icons.chair, "Sofa"),
                SizedBox(width: 80),
                _circleButton(Icons.airline_seat_individual_suite_rounded, "Bed"),
              ],
            ),
            SizedBox(height: 30),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _squareButton("Load", Icons.upload),
                SizedBox(width: 80),
                _squareButton("Save", Icons.save),
              ],
            ),
            Spacer(),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _reclinerButton(Icons.airline_seat_flat_angled, 1), // Relay 1
                SizedBox(width: 50),
                _reclinerButton(Icons.airline_seat_recline_normal, 2), // Relay 2
              ],
            ),
            SizedBox(height: 60),
          ],
        ),
      ),
    );
  }

  Widget _infoCard(String title, String value, IconData icon, {Color color = Colors.grey}) {
    return Container(
      width: 160,
      height: 200,
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black12, blurRadius: 5)],
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text(title, style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
          SizedBox(height: 25),
          // เพิ่มขนาดตัวเลขและจัดตำแหน่งให้น่าสนใจ
          Text(
            value,
            style: TextStyle(
              fontSize: 32, // ปรับขนาดตัวเลขให้ใหญ่ขึ้น
              fontWeight: FontWeight.bold,
              color: Colors.black, // สีตัวอักษร
            ),
            textAlign: TextAlign.center, // จัดตำแหน่งให้อยู่กลาง
          ),
          SizedBox(height: 30),
          Icon(icon, size: 50, color: color),
        ],
      ),
    );
  }


  Widget _circleButton(IconData icon, String label) {
    return Column(
      children: [
        ElevatedButton(
          onPressed: () {},
          style: ElevatedButton.styleFrom(
            shape: CircleBorder(),
            padding: EdgeInsets.all(30),
            backgroundColor: Colors.white,
            elevation: 4,
          ),
          child: Icon(icon, color: Colors.black, size: 40),
        ),
      ],
    );
  }

  Widget _squareButton(String label, IconData icon) {
    return ElevatedButton(
      onPressed: () {},
      style: ElevatedButton.styleFrom(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        padding: EdgeInsets.symmetric(horizontal: 28, vertical: 12),
        backgroundColor: Colors.white,
        elevation: 4,
      ),
      child: Column(
        children: [
          Icon(icon, color: Colors.black, size: 44),
          SizedBox(height: 4),
          Text(label, style: TextStyle(color: Colors.black, fontSize: 20)),
        ],
      ),
    );
  }

  Widget _reclinerButton(IconData icon, int relayNumber) {
    return GestureDetector(
      onTapDown: (_) {
        // เมื่อกดปุ่ม
        sendCommand(relayNumber == 1 ? "ON1" : "ON2");
      },
      onTapUp: (_) {
        // เมื่อปล่อยปุ่ม
        sendCommand(relayNumber == 1 ? "OFF1" : "OFF2");
      },
      onTapCancel: () {
        // กรณีที่ปุ่มถูกปล่อยออกก่อน
        sendCommand(relayNumber == 1 ? "OFF1" : "OFF2");
      },
      child: ElevatedButton(
        onPressed: () {}, // ให้สามารถกดได้
        style: ElevatedButton.styleFrom(
          shape: CircleBorder(),
          padding: EdgeInsets.all(34),
          backgroundColor: Colors.white, // กำหนดสีพื้นหลังที่ต้องการ
          elevation: 4,
          side: BorderSide(color: Colors.blue, width: 6),
        ),
        child: Icon(icon, color: Colors.blue, size: 62),
      ),
    );
  }

}
