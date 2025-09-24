import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'dart:convert';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // ล็อกหน้าจอเป็นแนวตั้ง
  await SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);

  runApp(MyApp());
}

// ----------------- MaterialApp -----------------
class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: HomePage(),
    );
  }
}

// ----------------- HomePage StatefulWidget -----------------
class HomePage extends StatefulWidget {
  @override
  _HomePageState createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with SingleTickerProviderStateMixin {
  // ----------------- BLE -----------------
  BluetoothDevice? esp32Device;
  BluetoothDevice? foundDevice;
  BluetoothCharacteristic? commandCharacteristic;
  BluetoothCharacteristic? sensorCharacteristic;

  bool isConnected = false;
  bool isReconnecting = false;
  bool isCooldown = false;

  String connectionStatus = "รอเชื่อมต่อ...";
  String temperature = "...";
  String humidity = "...";
  String mq2Value = "...";

  final String SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
  final String CHARACTERISTIC_UUID = "abcd1234-5678-1234-5678-abcdef123456";
  final String SENSOR_CHARACTERISTIC_UUID = "1234abcd-5678-1234-5678-abcdef654321";

  DateTime lastReconnect = DateTime.fromMillisecondsSinceEpoch(0);
  late AnimationController _controller;

  @override
  void initState() {
    super.initState();
    scanDevices();
    _controller = AnimationController(duration: Duration(seconds: 1), vsync: this);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  // ----------------- สแกนอุปกรณ์ -----------------
  void scanDevices() async {
    setState(() {
      connectionStatus = "กำลังค้นหาอุปกรณ์...";
      isConnected = false;
    });

    try {
      await FlutterBluePlus.startScan(timeout: Duration(seconds: 5));

      bool deviceFound = false;

      FlutterBluePlus.scanResults.listen((results) {
        for (ScanResult r in results) {
          if (r.device.name == "ESP32_BLE_Sofa2") {
            FlutterBluePlus.stopScan();
            foundDevice = r.device;
            deviceFound = true;
            connectToDevice(r.device);
            return;
          }
        }
      });

      Future.delayed(Duration(seconds: 5), () {
        if (!deviceFound && mounted) {
          FlutterBluePlus.stopScan();
          setState(() {
            connectionStatus = "ไม่พบอุปกรณ์";
            isConnected = false;
          });
          showStatus("ไม่พบอุปกรณ์ ESP32", Colors.red);
        }
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        connectionStatus = "กรุณาเปิดบลูทูธ";
        isConnected = false;
      });
      showStatus("กรุณาเปิดบลูทูธ", Colors.red);
    }
  }

  // ----------------- ค้นหา Services & Characteristics -----------------
  Future<void> discoverServicesAndCharacteristics(BluetoothDevice device) async {
    List<BluetoothService> services = await device.discoverServices();

    for (var service in services) {
      if (service.uuid.toString() == SERVICE_UUID) {
        for (var characteristic in service.characteristics) {
          if (characteristic.uuid.toString() == CHARACTERISTIC_UUID) {
            commandCharacteristic = characteristic;
          } else if (characteristic.uuid.toString() == SENSOR_CHARACTERISTIC_UUID) {
            sensorCharacteristic = characteristic;
            await sensorCharacteristic!.setNotifyValue(true);
            sensorCharacteristic!.value.listen(_onSensorData);
          }
        }
      }
    }
  }

  // ----------------- รับข้อมูล sensor -----------------
  void _onSensorData(List<int> value) {
    String data = utf8.decode(value);
    if (data.contains(',')) {
      List<String> sensors = data.split(',');
      if (sensors.length == 3 && mounted) {
        setState(() {
          temperature = sensors[0];
          humidity = sensors[1];
          mq2Value = sensors[2];
        });
      }
    } else if (data.trim().isNotEmpty) {
      _showDialog(data);
    }
  }

  // ----------------- เชื่อมต่อ -----------------
  void connectToDevice(BluetoothDevice device) async {
    try {
      await device.connect(timeout: Duration(seconds: 10));

      device.state.listen((state) {
        if (!mounted) return;
        if (state == BluetoothDeviceState.disconnected) {
          setState(() {
            isConnected = false;
            connectionStatus = "หลุดการเชื่อมต่อ กำลังพยายามเชื่อมต่อใหม่...";
          });
          showStatus("หลุดการเชื่อมต่อ กำลังพยายามเชื่อมต่อใหม่...", Colors.orange);
          reconnect();
        } else if (state == BluetoothDeviceState.connected) {
          setState(() {
            isConnected = true;
            connectionStatus = "เชื่อมต่อแล้ว";
          });
          showStatus("เชื่อมต่อโซฟา สำเร็จ", Colors.green);
        }
      });

      await discoverServicesAndCharacteristics(device);

      if (!mounted) return;
      setState(() {
        esp32Device = device;
        isConnected = true;
        connectionStatus = "เชื่อมต่อแล้ว";
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        connectionStatus = "เชื่อมต่อไม่สำเร็จ";
        isConnected = false;
      });
      showStatus("เชื่อมต่อโซฟา ไม่สำเร็จ", Colors.red);
    }
  }

  // ----------------- Reconnect -----------------
  void reconnect() async {
    DateTime now = DateTime.now();
    if (now.difference(lastReconnect).inSeconds < 3) return;
    lastReconnect = now;

    if (foundDevice != null) {
      setState(() => isReconnecting = true);
      _controller.repeat();
      showStatus("กำลังพยายามเชื่อมต่อใหม่...", Colors.orange);

      try {
        await foundDevice!.connect();
        await discoverServicesAndCharacteristics(foundDevice!);
        if (!mounted) return;
        setState(() {
          isConnected = true;
          connectionStatus = "เชื่อมต่อแล้ว";
        });
        showStatus("เชื่อมต่อโซฟา สำเร็จ", Colors.green);
      } catch (e) {
        if (!mounted) return;
        setState(() {
          isConnected = false;
          connectionStatus = "เชื่อมต่อไม่สำเร็จ";
        });
        showStatus("เชื่อมต่อโซฟา ไม่สำเร็จ", Colors.red);
      } finally {
        if (!mounted) return;
        setState(() => isReconnecting = false);
        _controller.stop();
      }
    } else {
      scanDevices();
    }
  }

  // ----------------- ส่งคำสั่ง -----------------
  void sendCommand(String command) async {
    if (commandCharacteristic != null && isConnected) {
      try {
        await commandCharacteristic!.write(command.codeUnits);
      } catch (e) {
        if (!mounted) return;
        setState(() => connectionStatus = "ส่งคำสั่งล้มเหลว");
        showStatus("ส่งคำสั่งล้มเหลว", Colors.red);
      }
    } else {
      if (!mounted) return;
      setState(() => connectionStatus = "ไม่ได้เชื่อมต่อ");
      showStatus("ไม่ได้เชื่อมต่อ", Colors.red);
    }
  }

  // ----------------- SnackBar -----------------
  void showStatus(String message, Color color) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(
      content: Text(message),
      backgroundColor: color,
      duration: Duration(seconds: 2),
    ));
  }

  // ----------------- AlertDialog -----------------
  void _showDialog(String message) {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) {
        Future.delayed(Duration(seconds: 5), () {
          if (Navigator.of(context).canPop()) Navigator.of(context).pop();
        });
        return AlertDialog(
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
          title: Text("แจ้งเตือน", style: TextStyle(fontWeight: FontWeight.bold, color: Colors.blue)),
          content: Text(message, style: TextStyle(fontSize: 18)),
          actions: [
            TextButton(
              child: Text("ปิด", style: TextStyle(color: Colors.blue)),
              onPressed: () => Navigator.of(context).pop(),
            ),
          ],
        );
      },
    );
  }

  // ----------------- Cooldown 8 sec -----------------
  void triggerCooldown() {
    setState(() => isCooldown = true);
    Future.delayed(Duration(seconds: 8), () {
      if (mounted) setState(() => isCooldown = false);
    });
  }

  // ----------------- Dialog เลือกพรีเซต -----------------
  void showPresetDialog({required bool isLoad}) {
    showDialog(
      context: context,
      builder: (context) {
        return AlertDialog(
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
          title: Text(isLoad ? "เลือกตำแหน่งปรับเอน" : "เลือกตำแหน่งบันทึก"),
          content: Row(
            mainAxisAlignment: MainAxisAlignment.spaceEvenly,
            children: List.generate(3, (index) {
              int preset = index + 1;
              return GestureDetector(
                onTap: () {
                  Navigator.of(context).pop(); // ปิด Dialog
                  triggerCooldown();
                  if (isLoad) {
                    sendCommand("AUTO$preset");
                  } else {
                    sendCommand("SAVE$preset");
                  }
                },
                child: Container(
                  width: 70,
                  height: 70,
                  decoration: BoxDecoration(
                    color: Colors.blue,
                    borderRadius: BorderRadius.circular(12),
                    boxShadow: [BoxShadow(color: Colors.black26, blurRadius: 4)],
                  ),
                  child: Center(
                    child: Text(
                      "$preset",
                      style: TextStyle(fontSize: 28, color: Colors.white, fontWeight: FontWeight.bold),
                    ),
                  ),
                ),
              );
            }),
          ),
        );
      },
    );
  }

  // ----------------- UI -----------------
  @override
  Widget build(BuildContext context) {
    return Scaffold(
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
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Icon(Icons.home, size: 62, color: Colors.blue),
                SizedBox(width: 10),
                Expanded(
                  child: InkWell(
                    borderRadius: BorderRadius.circular(10),
                    onTap: reconnect,
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
                          Expanded(
                            child: Text(
                              connectionStatus,
                              style: TextStyle(color: Colors.black, fontSize: 18),
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          SizedBox(width: 10),
                          isReconnecting
                              ? RotationTransition(turns: _controller, child: Icon(Icons.refresh, color: Colors.blue))
                              : Icon(Icons.refresh, color: Colors.blue),
                        ],
                      ),
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
              _circleButton(Icons.chair, onPressed: () {
                triggerCooldown();
                sendCommand("Sit");
              }),
              SizedBox(width: 80),
              _circleButton(Icons.airline_seat_individual_suite_rounded, onPressed: () {
                triggerCooldown();
                sendCommand("Lie");
              }),
            ],
          ),

          SizedBox(height: 30),

          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              _squareButton("Auto", Icons.upload, onPressed: () {
                showPresetDialog(isLoad: true); // ใช้ Dialog 3 ช่อง
              }),
              SizedBox(width: 80),
              _squareButton("Save", Icons.save, onPressed: () {
                showPresetDialog(isLoad: false); // ใช้ Dialog 3 ช่อง
              }),
            ],
          ),

          Spacer(),

          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              _reclinerButton(Icons.airline_seat_legroom_normal_outlined, 1),
              SizedBox(width: 50),
              _reclinerButton(Icons.airline_seat_flat, 2),
            ],
          ),

          SizedBox(height: 60),
        ],
      ),
    );
  }

  // ----------------- Info Card -----------------
  Widget _infoCard(String title, String value, IconData icon, {Color color = Colors.grey}) {
    Color bgColor = Colors.white;

    if (title == "ppm") {
      double? ppm = double.tryParse(value);
      if (ppm != null) {
        if (ppm <= 800) bgColor = Colors.green[200]!;
        else if (ppm <= 1400) bgColor = Colors.orange[300]!;
        else bgColor = Colors.red[300]!;
      }
    } else if (title == "Temp") {
      double? temp = double.tryParse(value);
      if (temp != null) {
        if (temp <= 35) bgColor = Colors.green[200]!;
        else if (temp <= 45) bgColor = Colors.orange[300]!;
        else bgColor = Colors.red[300]!;
      }
    }

    return Container(
      width: 160,
      height: 200,
      decoration: BoxDecoration(
        color: bgColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black12, blurRadius: 5)],
      ),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text(title, style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
          SizedBox(height: 25),
          Text(value, style: TextStyle(fontSize: 32, fontWeight: FontWeight.bold, color: Colors.black), textAlign: TextAlign.center),
          SizedBox(height: 30),
          Icon(icon, size: 50, color: color),
        ],
      ),
    );
  }

  // ----------------- Circle Button -----------------
  Widget _circleButton(IconData icon, {required VoidCallback onPressed}) {
    return ElevatedButton(
      onPressed: isCooldown ? null : onPressed,
      style: ElevatedButton.styleFrom(
        shape: CircleBorder(),
        padding: EdgeInsets.all(30),
        backgroundColor: Colors.white,
        elevation: 4,
      ),
      child: Icon(icon, color: isCooldown ? Colors.grey : Colors.black, size: 40),
    );
  }

  // ----------------- Square Button -----------------
  Widget _squareButton(String label, IconData icon, {required VoidCallback onPressed}) {
    return ElevatedButton(
      onPressed: isCooldown ? null : onPressed,
      style: ElevatedButton.styleFrom(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        padding: EdgeInsets.symmetric(horizontal: 28, vertical: 12),
        backgroundColor: Colors.white,
        elevation: 4,
      ),
      child: Column(
        children: [
          Icon(icon, color: isCooldown ? Colors.grey : Colors.black, size: 44),
          SizedBox(height: 4),
          Text(label, style: TextStyle(color: Colors.black, fontSize: 20)),
        ],
      ),
    );
  }

  // ----------------- Recliner Button -----------------
  Widget _reclinerButton(IconData icon, int relayNumber) {
    return GestureDetector(
      onTapDown: (_) {
        sendCommand(relayNumber == 1 ? "ON1" : "ON2");
      },
      onTapUp: (_) {
        sendCommand(relayNumber == 1 ? "OFF1" : "OFF2");
      },
      onTapCancel: () {
        sendCommand(relayNumber == 1 ? "OFF1" : "OFF2");
      },
      child: ElevatedButton(
        onPressed: () {},
        style: ElevatedButton.styleFrom(
          shape: CircleBorder(),
          padding: EdgeInsets.all(34),
          backgroundColor: Colors.white,
          elevation: 4,
          side: BorderSide(color: Colors.blue, width: 6),
        ),
        child: Icon(icon, color: Colors.blue, size: 62),
      ),
    );
  }
}
