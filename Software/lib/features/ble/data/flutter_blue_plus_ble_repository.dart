import 'dart:async';
import 'dart:typed_data';

import 'package:universal_ble/universal_ble.dart';

import '../domain/ble_connection_state.dart';
import '../domain/ble_device_summary.dart';
import '../../core/constants/ble_uuids.dart';

class FlutterBluePlusBleRepository {
  String? _connectedAddress;

  final _scanCtrl = StreamController<List<BleDeviceSummary>>.broadcast();
  final _statusCtrl = StreamController<BleConnectionStatus>.broadcast();

  Stream<List<BleDeviceSummary>> get scanResults => _scanCtrl.stream;
  Stream<BleConnectionStatus> get status => _statusCtrl.stream;

  Future<void> startScan() async {
    _statusCtrl.add(BleConnectionStatus.scanning);
    final devices = <String, BleDevice>{};

    UniversalBle.onScanResult = (device) {
      if (device.name != null && device.name!.isNotEmpty) {
        devices[device.deviceId] = device;
        _scanCtrl.add(devices.values
            .map((d) => BleDeviceSummary(id: d.deviceId, name: d.name ?? '', rssi: 0))
            .toList());
      }
    };

    await UniversalBle.startScan();

    Future.delayed(const Duration(seconds: 5), () {
      UniversalBle.stopScan();
      _statusCtrl.add(BleConnectionStatus.idle);
    });
  }

  Future<void> stopScan() async {
    UniversalBle.stopScan();
    _statusCtrl.add(BleConnectionStatus.idle);
  }

  Future<void> connect(String id) async {
    _statusCtrl.add(BleConnectionStatus.connecting);
    await UniversalBle.disconnect(id).catchError((_) {});
    await UniversalBle.connect(id);
    _connectedAddress = id;
    await Future.delayed(const Duration(milliseconds: 1500));
    await UniversalBle.discoverServices(id);
    _statusCtrl.add(BleConnectionStatus.connected);
  }

  Future<void> disconnect() async {
    if (_connectedAddress != null) {
      await UniversalBle.disconnect(_connectedAddress!).catchError((_) {});
      _connectedAddress = null;
    }
    _statusCtrl.add(BleConnectionStatus.disconnected);
  }

  Future<String> readHandshake() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    final data = await UniversalBle.readValue(_connectedAddress!, BleUuids.configService, BleUuids.infoChar);
    return String.fromCharCodes(data);
  }

  Future<Uint8List> readProfile() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    final data = await UniversalBle.readValue(_connectedAddress!, BleUuids.configService, BleUuids.dumpChar);
    return Uint8List.fromList(data);
  }

  Future<Uint8List> readSequences() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    final data = await UniversalBle.readValue(_connectedAddress!, BleUuids.configService, BleUuids.sequencesChar);
    return Uint8List.fromList(data);
  }

  Future<void> writeProfile(Uint8List data) async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    await UniversalBle.writeValue(_connectedAddress!, BleUuids.configService, BleUuids.ctrlChar, data, BleOutputProperty.withResponse);
  }

  Future<void> writeControl(Uint8List data) async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    await UniversalBle.writeValue(_connectedAddress!, BleUuids.configService, BleUuids.ctrlChar, data, BleOutputProperty.withResponse);
  }
}
