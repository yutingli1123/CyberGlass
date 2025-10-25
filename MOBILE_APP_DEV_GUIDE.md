# CyberGlass Mobile App 开发指南 (Flutter)

## 概述

本指南介绍如何使用 **Flutter** 开发跨平台移动应用来连接和控制 CyberGlass 设备。应用通过 BLE 获取 WiFi 凭据，然后连接到设备的 AP 并控制摄像头。

**优势**:
- ✅ 一套代码，同时支持 iOS 和 Android
- ✅ 快速开发，热重载
- ✅ 丰富的插件生态
- ✅ 原生性能

## 应用架构

```
┌─────────────────────────────────────┐
│    Flutter App (iOS & Android)       │
│  ┌────────────┐      ┌────────────┐ │
│  │flutter_blue│      │ wifi_iot   │ │
│  │  (BLE)     │      │  (WiFi)    │ │
│  └─────┬──────┘      └─────┬──────┘ │
│        │                   │         │
│  ┌─────▼───────────────────▼──────┐ │
│  │     Camera Control UI            │ │
│  │        (Flutter Widgets)         │ │
│  └──────────────────────────────────┘ │
└───────────┬──────────────────────────┘
            │ BLE / WiFi / HTTP
            ▼
    ┌──────────────┐
    │  CyberGlass  │
    │   ESP32S3    │
    └──────────────┘
```

## 开发流程

### 阶段 1: BLE 扫描与配网
1. 扫描附近的 BLE 设备
2. 筛选出 `CyberGlass-` 前缀的设备
3. 连接 BLE 并读取 WiFi 凭据
4. 显示设备信息给用户

### 阶段 2: WiFi 连接
1. 使用 BLE 获取的凭据连接 WiFi AP
2. 验证连接成功 (Ping 192.168.4.1)
3. 保存设备信息到本地

### 阶段 3: 摄像头控制
1. 通过 HTTP API 控制摄像头
2. 显示 MJPEG 视频流
3. 提供拍照、录像等功能

---

## Flutter 项目设置

### 1. 环境要求

- **Flutter SDK**: 3.0+
- **Dart SDK**: 2.17+
- **iOS**: Xcode 14+, iOS 12.0+
- **Android**: Android Studio, API 21+ (Android 5.0+)

### 2. 创建项目

```bash
# 创建新的 Flutter 项目
flutter create cyberglass_control

cd cyberglass_control

# 运行
flutter run
```

### 3. 添加依赖 (pubspec.yaml)

```yaml
name: cyberglass_control
description: CyberGlass Camera Control App

environment:
  sdk: '>=2.17.0 <4.0.0'

dependencies:
  flutter:
    sdk: flutter

  # BLE 支持
  flutter_blue_plus: ^1.14.0

  # WiFi 连接 (Android)
  wifi_iot: ^0.3.18

  # HTTP 请求
  http: ^1.1.0
  dio: ^5.3.0

  # 状态管理
  provider: ^6.0.5
  get: ^4.6.5

  # 本地存储
  shared_preferences: ^2.2.0

  # 图片显示
  cached_network_image: ^3.3.0

  # 权限管理
  permission_handler: ^11.0.0

dev_dependencies:
  flutter_test:
    sdk: flutter
  flutter_lints: ^2.0.0
```

### 4. 平台配置

#### iOS 配置 (ios/Runner/Info.plist)

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>需要蓝牙权限以发现和连接 CyberGlass 设备</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>需要蓝牙权限以读取设备 WiFi 凭据</string>

<key>NSLocalNetworkUsageDescription</key>
<string>需要访问本地网络以控制摄像头</string>

<key>UIBackgroundModes</key>
<array>
    <string>bluetooth-central</string>
</array>
```

#### Android 配置 (android/app/src/main/AndroidManifest.xml)

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- 蓝牙权限 -->
    <uses-permission android:name="android.permission.BLUETOOTH" />
    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
    <uses-permission android:name="android.permission.BLUETOOTH_SCAN"
                     android:usesPermissionFlags="neverForLocation" />
    <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

    <!-- 位置权限 (BLE 扫描需要) -->
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
    <uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />

    <!-- WiFi 权限 -->
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
    <uses-permission android:name="android.permission.CHANGE_WIFI_STATE" />
    <uses-permission android:name="android.permission.CHANGE_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <!-- 网络权限 -->
    <uses-permission android:name="android.permission.INTERNET" />

    <application
        android:label="CyberGlass Control"
        android:icon="@mipmap/ic_launcher">
        <!-- ... -->
    </application>
</manifest>
```

---

## 核心代码实现

### 项目结构

```
lib/
├── main.dart                    # 应用入口
├── models/
│   └── device_info.dart        # 设备信息模型
├── services/
│   ├── ble_service.dart        # BLE 服务
│   ├── wifi_service.dart       # WiFi 服务
│   └── camera_service.dart     # 摄像头 API 服务
├── providers/
│   └── app_state.dart          # 全局状态管理
└── screens/
    ├── scanner_screen.dart     # BLE 扫描界面
    ├── connection_screen.dart  # WiFi 连接界面
    └── camera_screen.dart      # 摄像头控制界面
```

---

### 1. 设备信息模型 (lib/models/device_info.dart)

```dart
class DeviceInfo {
  final String deviceId;
  final String name;
  final String ssid;
  final String password;
  final String address;

  DeviceInfo({
    required this.deviceId,
    required this.name,
    required this.ssid,
    required this.password,
    required this.address,
  });

  // 转换为 JSON
  Map<String, dynamic> toJson() {
    return {
      'deviceId': deviceId,
      'name': name,
      'ssid': ssid,
      'password': password,
      'address': address,
    };
  }

  // 从 JSON 创建
  factory DeviceInfo.fromJson(Map<String, dynamic> json) {
    return DeviceInfo(
      deviceId: json['deviceId'] ?? '',
      name: json['name'] ?? '',
      ssid: json['ssid'] ?? '',
      password: json['password'] ?? '',
      address: json['address'] ?? '',
    );
  }
}
```

---

### 2. BLE 服务 (lib/services/ble_service.dart)

```dart
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'dart:async';

class BLEService {
  // CyberGlass UUIDs
  static final Guid serviceUuid = Guid("c153f40c-b1eb-11f0-bf8b-dbb9b632dc78");
  static final Guid ssidCharUuid = Guid("c5dad860-b1eb-11f0-96db-bb1907c420ec");
  static final Guid passwordCharUuid = Guid("c8ee211a-b1eb-11f0-bfc4-777d4dda4cde");

  final FlutterBluePlus _flutterBlue = FlutterBluePlus();

  StreamController<List<ScanResult>> _scanResultsController =
      StreamController<List<ScanResult>>.broadcast();
  Stream<List<ScanResult>> get scanResults => _scanResultsController.stream;

  StreamController<String> _statusController =
      StreamController<String>.broadcast();
  Stream<String> get connectionStatus => _statusController.stream;

  BluetoothDevice? _connectedDevice;
  String? _wifiSSID;
  String? _wifiPassword;

  String? get wifiSSID => _wifiSSID;
  String? get wifiPassword => _wifiPassword;

  // 开始扫描 CyberGlass 设备
  Future<void> startScanning() async {
    _statusController.add("扫描中...");

    List<ScanResult> devices = [];

    // 监听扫描结果
    FlutterBluePlus.scanResults.listen((results) {
      // 只保留 CyberGlass 设备
      devices = results.where((result) {
        return result.device.platformName.startsWith('CyberGlass-');
      }).toList();

      _scanResultsController.add(devices);
    });

    // 开始扫描 (扫描 4 秒)
    await FlutterBluePlus.startScan(
      withServices: [serviceUuid],
      timeout: Duration(seconds: 4),
    );

    _statusController.add("扫描完成");
  }

  // 停止扫描
  Future<void> stopScanning() async {
    await FlutterBluePlus.stopScan();
    _statusController.add("扫描停止");
  }

  // 连接设备并读取 WiFi 凭据
  Future<bool> connectAndReadCredentials(BluetoothDevice device) async {
    try {
      _statusController.add("连接中...");

      // 连接设备
      await device.connect(timeout: Duration(seconds: 10));
      _connectedDevice = device;
      _statusController.add("已连接");

      // 发现服务
      List<BluetoothService> services = await device.discoverServices();

      BluetoothService? targetService;
      for (var service in services) {
        if (service.uuid == serviceUuid) {
          targetService = service;
          break;
        }
      }

      if (targetService == null) {
        _statusController.add("未找到服务");
        return false;
      }

      // 读取 SSID
      for (var characteristic in targetService.characteristics) {
        if (characteristic.uuid == ssidCharUuid) {
          List<int> value = await characteristic.read();
          _wifiSSID = String.fromCharCodes(value);
          _statusController.add("SSID 已获取");
        } else if (characteristic.uuid == passwordCharUuid) {
          List<int> value = await characteristic.read();
          _wifiPassword = String.fromCharCodes(value);
          _statusController.add("凭据获取成功！");
        }
      }

      return _wifiSSID != null && _wifiPassword != null;
    } catch (e) {
      _statusController.add("连接失败: $e");
      return false;
    }
  }

  // 断开连接
  Future<void> disconnect() async {
    if (_connectedDevice != null) {
      await _connectedDevice!.disconnect();
      _connectedDevice = null;
      _statusController.add("已断开");
    }
  }

  // 清理资源
  void dispose() {
    _scanResultsController.close();
    _statusController.close();
  }
}
```

---

### 3. WiFi 服务 (lib/services/wifi_service.dart)

```dart
import 'package:wifi_iot/wifi_iot.dart';
import 'package:http/http.dart' as http;
import 'dart:async';

class WiFiService {
  bool _isConnected = false;
  String? _currentSSID;

  bool get isConnected => _isConnected;
  String? get currentSSID => _currentSSID;

  // 连接到 WiFi
  Future<bool> connectToWiFi(String ssid, String password) async {
    try {
      // 使用 wifi_iot 包连接
      bool result = await WiFiForIoTPlugin.connect(
        ssid,
        password: password,
        security: NetworkSecurity.WPA,
        joinOnce: false, // 保持连接
      );

      if (result) {
        _isConnected = true;
        _currentSSID = ssid;

        // 验证连接
        bool verified = await verifyConnection();
        return verified;
      }

      return false;
    } catch (e) {
      print("WiFi 连接失败: $e");
      return false;
    }
  }

  // 验证连接 (Ping 设备)
  Future<bool> verifyConnection() async {
    try {
      final response = await http.get(
        Uri.parse('http://192.168.4.1/status'),
      ).timeout(Duration(seconds: 5));

      return response.statusCode == 200;
    } catch (e) {
      print("验证连接失败: $e");
      return false;
    }
  }

  // 断开 WiFi
  Future<void> disconnect() async {
    await WiFiForIoTPlugin.disconnect();
    _isConnected = false;
    _currentSSID = null;
  }

  // 获取当前连接的 SSID
  Future<String?> getCurrentSSID() async {
    return await WiFiForIoTPlugin.getSSID();
  }
}
```

---

### 4. 摄像头 API 服务 (lib/services/camera_service.dart)

```dart
import 'package:http/http.dart' as http;
import 'dart:typed_data';
import 'dart:convert';

class CameraService {
  static const String baseUrl = "http://192.168.4.1";

  // 拍照
  Future<Uint8List?> capturePhoto() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/capture'));

      if (response.statusCode == 200) {
        return response.bodyBytes;
      }
      return null;
    } catch (e) {
      print("拍照失败: $e");
      return null;
    }
  }

  // 获取视频流 URL
  String getStreamUrl() {
    return '$baseUrl/stream';
  }

  // 停止视频流
  Future<bool> stopStream() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/stop'));
      return response.statusCode == 200;
    } catch (e) {
      print("停止视频流失败: $e");
      return false;
    }
  }

  // 改变分辨率
  Future<bool> changeResolution(String resolution) async {
    try {
      final response = await http.get(
        Uri.parse('$baseUrl/resolution?value=$resolution'),
      );
      return response.statusCode == 200;
    } catch (e) {
      print("改变分辨率失败: $e");
      return false;
    }
  }

  // 改变质量
  Future<bool> changeQuality(int quality) async {
    try {
      final response = await http.get(
        Uri.parse('$baseUrl/quality?value=$quality'),
      );
      return response.statusCode == 200;
    } catch (e) {
      print("改变质量失败: $e");
      return false;
    }
  }

  // 获取状态
  Future<Map<String, dynamic>?> getStatus() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/status'));

      if (response.statusCode == 200) {
        return json.decode(response.body) as Map<String, dynamic>;
      }
      return null;
    } catch (e) {
      print("获取状态失败: $e");
      return null;
    }
  }
}
```

---

### 5. BLE 扫描界面 (lib/screens/scanner_screen.dart)

```dart
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../services/ble_service.dart';

class ScannerScreen extends StatefulWidget {
  final BLEService bleService;

  const ScannerScreen({Key? key, required this.bleService}) : super(key: key);

  @override
  _ScannerScreenState createState() => _ScannerScreenState();
}

class _ScannerScreenState extends State<ScannerScreen> {
  List<ScanResult> _devices = [];
  String _status = "未扫描";
  String? _wifiSSID;
  String? _wifiPassword;

  @override
  void initState() {
    super.initState();

    // 监听扫描结果
    widget.bleService.scanResults.listen((results) {
      setState(() {
        _devices = results;
      });
    });

    // 监听连接状态
    widget.bleService.connectionStatus.listen((status) {
      setState(() {
        _status = status;
        _wifiSSID = widget.bleService.wifiSSID;
        _wifiPassword = widget.bleService.wifiPassword;
      });
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('扫描 CyberGlass 设备'),
      ),
      body: Padding(
        padding: EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 状态显示
            Text(
              _status,
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
            ),
            SizedBox(height: 16),

            // 扫描按钮
            ElevatedButton(
              onPressed: () => widget.bleService.startScanning(),
              child: Text('开始扫描'),
            ),
            SizedBox(height: 16),

            // 设备列表
            Expanded(
              child: ListView.builder(
                itemCount: _devices.length,
                itemBuilder: (context, index) {
                  final device = _devices[index].device;
                  return Card(
                    child: ListTile(
                      title: Text(device.platformName),
                      subtitle: Text(device.remoteId.toString()),
                      trailing: Icon(Icons.bluetooth),
                      onTap: () async {
                        bool success = await widget.bleService
                            .connectAndReadCredentials(device);

                        if (success) {
                          // 跳转到连接界面
                          Navigator.pushNamed(
                            context,
                            '/connection',
                            arguments: {
                              'ssid': widget.bleService.wifiSSID,
                              'password': widget.bleService.wifiPassword,
                            },
                          );
                        }
                      },
                    ),
                  );
                },
              ),
            ),

            // 显示获取的凭据
            if (_wifiSSID != null && _wifiPassword != null)
              Card(
                color: Colors.green[50],
                child: Padding(
                  padding: EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'WiFi 凭据',
                        style: TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      SizedBox(height: 8),
                      Text('SSID: $_wifiSSID'),
                      Text('密码: $_wifiPassword'),
                      SizedBox(height: 8),
                      ElevatedButton(
                        onPressed: () {
                          Navigator.pushNamed(
                            context,
                            '/connection',
                            arguments: {
                              'ssid': _wifiSSID,
                              'password': _wifiPassword,
                            },
                          );
                        },
                        child: Text('连接 WiFi'),
                      ),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
```

---

### 6. WiFi 连接界面 (lib/screens/connection_screen.dart)

```dart
import 'package:flutter/material.dart';
import '../services/wifi_service.dart';

class ConnectionScreen extends StatefulWidget {
  final WiFiService wifiService;

  const ConnectionScreen({Key? key, required this.wifiService})
      : super(key: key);

  @override
  _ConnectionScreenState createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends State<ConnectionScreen> {
  bool _isConnecting = false;
  String _status = "准备连接";

  @override
  Widget build(BuildContext context) {
    // 获取传递的参数
    final args = ModalRoute.of(context)!.settings.arguments as Map<String, String>;
    final ssid = args['ssid']!;
    final password = args['password']!;

    return Scaffold(
      appBar: AppBar(
        title: Text('连接 WiFi'),
      ),
      body: Padding(
        padding: EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'WiFi 信息',
              style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
            ),
            SizedBox(height: 16),
            Text('SSID: $ssid', style: TextStyle(fontSize: 18)),
            Text('密码: $password', style: TextStyle(fontSize: 18)),
            SizedBox(height: 32),

            // 状态显示
            Text(
              _status,
              style: TextStyle(fontSize: 16, color: Colors.blue),
            ),
            SizedBox(height: 16),

            // 连接按钮
            _isConnecting
                ? CircularProgressIndicator()
                : ElevatedButton(
                    onPressed: () async {
                      setState(() {
                        _isConnecting = true;
                        _status = "连接中...";
                      });

                      bool success = await widget.wifiService.connectToWiFi(
                        ssid,
                        password,
                      );

                      setState(() {
                        _isConnecting = false;
                        _status = success ? "连接成功！" : "连接失败";
                      });

                      if (success) {
                        // 跳转到摄像头控制界面
                        Navigator.pushReplacementNamed(context, '/camera');
                      }
                    },
                    child: Text('连接 WiFi'),
                  ),
          ],
        ),
      ),
    );
  }
}
```

---

### 7. 摄像头控制界面 (lib/screens/camera_screen.dart)

```dart
import 'package:flutter/material.dart';
import 'package:cached_network_image/cached_network_image.dart';
import '../services/camera_service.dart';
import 'dart:typed_data';

class CameraScreen extends StatefulWidget {
  final CameraService cameraService;

  const CameraScreen({Key? key, required this.cameraService}) : super(key: key);

  @override
  _CameraScreenState createState() => _CameraScreenState();
}

class _CameraScreenState extends State<CameraScreen> {
  bool _isStreaming = false;
  String _resolution = 'VGA';
  double _quality = 10.0;
  Uint8List? _capturedPhoto;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('摄像头控制'),
      ),
      body: SingleChildScrollView(
        child: Padding(
          padding: EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 视频流显示
              Container(
                height: 300,
                color: Colors.black,
                child: _isStreaming
                    ? CachedNetworkImage(
                        imageUrl: widget.cameraService.getStreamUrl(),
                        placeholder: (context, url) =>
                            Center(child: CircularProgressIndicator()),
                        errorWidget: (context, url, error) =>
                            Center(child: Icon(Icons.error, color: Colors.red)),
                      )
                    : Center(
                        child: Text(
                          '点击开始视频流',
                          style: TextStyle(color: Colors.white),
                        ),
                      ),
              ),
              SizedBox(height: 16),

              // 控制按钮
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  ElevatedButton.icon(
                    onPressed: () async {
                      Uint8List? photo = await widget.cameraService.capturePhoto();
                      if (photo != null) {
                        setState(() {
                          _capturedPhoto = photo;
                        });
                        ScaffoldMessenger.of(context).showSnackBar(
                          SnackBar(content: Text('拍照成功')),
                        );
                      }
                    },
                    icon: Icon(Icons.camera_alt),
                    label: Text('拍照'),
                  ),
                  ElevatedButton.icon(
                    onPressed: () {
                      setState(() {
                        _isStreaming = !_isStreaming;
                      });

                      if (!_isStreaming) {
                        widget.cameraService.stopStream();
                      }
                    },
                    icon: Icon(_isStreaming ? Icons.stop : Icons.play_arrow),
                    label: Text(_isStreaming ? '停止' : '开始'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: _isStreaming ? Colors.red : Colors.green,
                    ),
                  ),
                ],
              ),
              SizedBox(height: 24),

              // 分辨率选择
              Text('分辨率', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
              SegmentedButton<String>(
                segments: [
                  ButtonSegment(value: 'QVGA', label: Text('QVGA')),
                  ButtonSegment(value: 'VGA', label: Text('VGA')),
                  ButtonSegment(value: 'HD', label: Text('HD')),
                  ButtonSegment(value: 'UXGA', label: Text('UXGA')),
                ],
                selected: {_resolution},
                onSelectionChanged: (Set<String> selected) async {
                  String newResolution = selected.first;
                  bool success = await widget.cameraService.changeResolution(newResolution);
                  if (success) {
                    setState(() {
                      _resolution = newResolution;
                    });
                  }
                },
              ),
              SizedBox(height: 24),

              // 质量调节
              Text('质量: ${_quality.toInt()}',
                   style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
              Slider(
                value: _quality,
                min: 5,
                max: 30,
                divisions: 5,
                label: _quality.toInt().toString(),
                onChanged: (double value) {
                  setState(() {
                    _quality = value;
                  });
                },
                onChangeEnd: (double value) async {
                  await widget.cameraService.changeQuality(value.toInt());
                },
              ),
              SizedBox(height: 24),

              // 显示拍摄的照片
              if (_capturedPhoto != null)
                Column(
                  children: [
                    Text('拍摄的照片',
                         style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
                    SizedBox(height: 8),
                    Image.memory(_capturedPhoto!, height: 200),
                  ],
                ),
            ],
          ),
        ),
      ),
    );
  }
}
```

---

### 8. 主应用入口 (lib/main.dart)

```dart
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'services/ble_service.dart';
import 'services/wifi_service.dart';
import 'services/camera_service.dart';
import 'screens/scanner_screen.dart';
import 'screens/connection_screen.dart';
import 'screens/camera_screen.dart';

void main() {
  runApp(CyberGlassApp());
}

class CyberGlassApp extends StatelessWidget {
  final BLEService bleService = BLEService();
  final WiFiService wifiService = WiFiService();
  final CameraService cameraService = CameraService();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'CyberGlass Control',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        useMaterial3: true,
      ),
      home: HomeScreen(bleService: bleService),
      routes: {
        '/scanner': (context) => ScannerScreen(bleService: bleService),
        '/connection': (context) => ConnectionScreen(wifiService: wifiService),
        '/camera': (context) => CameraScreen(cameraService: cameraService),
      },
    );
  }
}

class HomeScreen extends StatefulWidget {
  final BLEService bleService;

  const HomeScreen({Key? key, required this.bleService}) : super(key: key);

  @override
  _HomeScreenState createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  @override
  void initState() {
    super.initState();
    _requestPermissions();
  }

  // 请求权限
  Future<void> _requestPermissions() async {
    Map<Permission, PermissionStatus> statuses = await [
      Permission.bluetooth,
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();

    // 检查权限状态
    if (statuses.values.any((status) => !status.isGranted)) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('需要蓝牙和位置权限才能使用')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('CyberGlass Control'),
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(Icons.camera, size: 100, color: Colors.blue),
            SizedBox(height: 24),
            Text(
              'CyberGlass 设备控制',
              style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
            ),
            SizedBox(height: 16),
            Text(
              '通过 BLE 连接并控制摄像头',
              style: TextStyle(fontSize: 16, color: Colors.grey),
            ),
            SizedBox(height: 48),
            ElevatedButton.icon(
              onPressed: () {
                Navigator.pushNamed(context, '/scanner');
              },
              icon: Icon(Icons.bluetooth_searching),
              label: Text('扫描设备'),
              style: ElevatedButton.styleFrom(
                padding: EdgeInsets.symmetric(horizontal: 32, vertical: 16),
                textStyle: TextStyle(fontSize: 18),
              ),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    widget.bleService.dispose();
    super.dispose();
  }
}
```

---

### 构建和运行

```bash
# 安装依赖
flutter pub get

# 运行到 iOS 设备
flutter run -d ios

# 运行到 Android 设备
flutter run -d android

# 构建 APK (Android)
flutter build apk

# 构建 IPA (iOS, 需要 Mac)
flutter build ios
```

---

## 通用开发建议

### 1. 错误处理

```dart
// 超时处理
try {
  await bleService.connectAndReadCredentials(device)
      .timeout(Duration(seconds: 10));
} on TimeoutException {
  print("连接超时");
}

// 重试机制
Future<T> retry<T>(Future<T> Function() operation, {int maxAttempts = 3}) async {
  int attempts = 0;
  while (attempts < maxAttempts) {
    try {
      return await operation();
    } catch (e) {
      attempts++;
      if (attempts >= maxAttempts) rethrow;
      await Future.delayed(Duration(seconds: 2));
    }
  }
  throw Exception('Max retry attempts reached');
}

// 使用示例
await retry(() => cameraService.capturePhoto());
```

### 2. 用户体验优化

- **加载指示器**: BLE 扫描和 WiFi 连接时显示 CircularProgressIndicator
- **错误提示**: 使用 SnackBar 显示清晰的错误消息和解决建议
- **保存设备**: 使用 shared_preferences 本地保存已配对的设备信息
- **自动重连**: WiFi 断开时自动重连机制
- **状态管理**: 使用 Provider 或 GetX 进行全局状态管理

### 3. 性能优化

- **图片缓存**: 使用 cached_network_image 缓存图片
- **后台任务**: BLE 和网络操作使用 async/await
- **内存管理**: 及时 dispose StreamController 和其他资源
- **懒加载**: 使用 ListView.builder 而非 ListView

### 4. 安全考虑

- **HTTPS**: 生产环境使用 HTTPS (目前是本地网络 HTTP)
- **证书校验**: 生产环境验证服务器证书
- **密码存储**: 使用 flutter_secure_storage 安全存储敏感信息
- **权限最小化**: 只请求必要的权限

---

## 测试

### 单元测试

```dart
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('DeviceInfo', () {
    test('fromJson creates valid DeviceInfo', () {
      final json = {
        'deviceId': 'A1B2',
        'name': 'CyberGlass-A1B2',
        'ssid': 'CyberGlass-A1B2',
        'password': 'test123',
        'address': '00:11:22:33:44:55',
      };

      final device = DeviceInfo.fromJson(json);

      expect(device.deviceId, 'A1B2');
      expect(device.name, 'CyberGlass-A1B2');
    });

    test('device name starts with CyberGlass-', () {
      final device = DeviceInfo(
        deviceId: 'A1B2',
        name: 'CyberGlass-A1B2',
        ssid: 'CyberGlass-A1B2',
        password: 'test123',
        address: '00:11:22:33:44:55',
      );

      expect(device.name.startsWith('CyberGlass-'), true);
    });
  });
}
```

### Widget 测试

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter/material.dart';

void main() {
  testWidgets('Scanner screen has scan button', (WidgetTester tester) async {
    final bleService = BLEService();

    await tester.pumpWidget(
      MaterialApp(
        home: ScannerScreen(bleService: bleService),
      ),
    );

    expect(find.text('开始扫描'), findsOneWidget);
    expect(find.byType(ElevatedButton), findsWidgets);
  });
}
```

---

## 发布

### iOS
1. 在 Xcode 中配置签名证书
2. 配置 App Store Connect
3. 使用 `flutter build ios` 构建
4. 上传构建版本到 App Store Connect
5. 提交审核

### Android
1. 生成签名密钥:
   ```bash
   keytool -genkey -v -keystore ~/cyberglass-key.jks -keyalg RSA -keysize 2048 -validity 10000 -alias cyberglass
   ```
2. 配置 `android/key.properties`
3. 使用 `flutter build apk --release` 或 `flutter build appbundle`
4. 上传到 Google Play Console
5. 发布到内部测试/公开测试/生产

---

## 参考资源

### Flutter 和插件
- [Flutter 官方文档](https://docs.flutter.dev/)
- [flutter_blue_plus 插件](https://pub.dev/packages/flutter_blue_plus)
- [wifi_iot 插件](https://pub.dev/packages/wifi_iot)
- [permission_handler 插件](https://pub.dev/packages/permission_handler)
- [cached_network_image 插件](https://pub.dev/packages/cached_network_image)

### 蓝牙和网络
- [ESP32 BLE GATT 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gatts.html)
- [BLE GATT 服务规范](https://www.bluetooth.com/specifications/gatt/)

### Flutter 状态管理
- [Provider 包](https://pub.dev/packages/provider)
- [GetX 包](https://pub.dev/packages/get)

---

## 常见问题

**Q: BLE 扫描找不到设备？**

A:
- 检查蓝牙权限是否授予 (Android 需要位置权限)
- 确认蓝牙已开启
- 检查 ESP32 设备是否在广播 BLE
- iOS: 检查 Info.plist 中的权限描述
- Android: 检查 AndroidManifest.xml 中的权限声明

**Q: WiFi 连接失败？**

A:
- iOS: 使用真机测试 (模拟器不支持 WiFi 控制)
- Android 10+: 用户需要手动确认连接请求
- 检查密码是否正确
- 确认设备在 WiFi 范围内
- Android: 某些设备厂商限制了 WiFi API，需要用户手动连接

**Q: 视频流显示黑屏或卡顿？**

A:
- 降低分辨率 (使用 QVGA 或 VGA)
- 检查网络连接是否稳定
- 确认已连接到正确的 WiFi AP (192.168.4.1)
- MJPEG 流需要持续 HTTP 连接，检查是否超时

**Q: Flutter 编译错误？**

A:
- 运行 `flutter clean` 清理构建缓存
- 运行 `flutter pub get` 重新获取依赖
- 检查 Flutter SDK 版本 (需要 3.0+)
- iOS: 运行 `cd ios && pod install` 更新 CocoaPods
- Android: 检查 gradle 版本兼容性

**Q: 如何处理多设备？**

A:
- 使用 shared_preferences 保存设备列表
- 根据设备 ID 区分不同设备
- 实现设备列表页面，让用户选择要连接的设备
- 保存最近连接的设备，提供快速重连功能

**Q: iOS 权限请求无效？**

A:
- 确保 Info.plist 中有正确的权限描述文字
- 使用 permission_handler 插件请求权限
- iOS 13+ 蓝牙权限需要在首次使用时请求

**Q: Android WiFi 连接在不同版本表现不一致？**

A:
- Android 9 及以下: 使用 WifiManager.addNetwork()
- Android 10+: 使用 WifiNetworkSpecifier
- 某些厂商 ROM 可能限制 WiFi API，建议提供手动连接指引

---

## 完成后的功能

你的 Flutter App 将能够：
- ✅ **跨平台支持**: 一套代码同时支持 iOS 和 Android
- ✅ **自动发现设备**: 通过 BLE 自动扫描 CyberGlass 设备
- ✅ **无线配网**: 无需串口线，通过 BLE 获取 WiFi 凭据
- ✅ **自动连接**: 自动连接到设备 WiFi AP
- ✅ **摄像头控制**: 拍照、视频流、调整分辨率和质量
- ✅ **本地存储**: 保存设备信息供下次快速连接
- ✅ **错误处理**: 完善的错误提示和重试机制
- ✅ **现代 UI**: 使用 Material Design 3 设计

---

## 下一步优化

1. **状态管理**: 使用 Provider 或 Riverpod 进行更好的状态管理
2. **数据持久化**: 使用 sqflite 或 Hive 存储设备历史记录
3. **视频录制**: 实现视频流录制功能
4. **图片库**: 将拍摄的照片保存到相册
5. **通知**: 添加后台连接状态通知
6. **多语言**: 支持多语言国际化
7. **深色模式**: 支持深色主题
8. **离线模式**: 缓存设备信息，支持快速重连
