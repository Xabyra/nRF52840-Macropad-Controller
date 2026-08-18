import 'dart:typed_data';
import 'package:universal_ble/universal_ble.dart';

/// Абстрактний клас, що визначає контракт для роботи з BLE.
abstract class BleRepository {
  Stream<List<BleDevice>> scanDevices({Duration timeout = const Duration(seconds: 5)});
  Future<void> connect(String address);
  Future<void> disconnect(String address);
  Future<String> readInfoJson();
  Future<Uint8List> readProfile();
  Future<Uint8List> readSequences();
  Future<void> writeProfile(Uint8List bytes);
  Future<void> writeControl(Uint8List bytes);
  Stream<bool> connectionState(String address);
}

/// Мокова реалізація для тестування UI без реального пристрою.
class MockBleRepository implements BleRepository {
  @override
  Stream<List<BleDevice>> scanDevices({Duration timeout = const Duration(seconds: 5)}) async* {
    await Future.delayed(const Duration(seconds: 1));
    // Повертаємо порожній список, оскільки ScanResult складно імітувати без залежностей.
    // Основне тестування UI буде з реальною імплементацією.
    yield [];
  }

  @override
  Future<void> connect(String address) async =>
      Future.delayed(const Duration(milliseconds: 500));

  @override
  Future<String> readInfoJson() async => '{"ver":"1.0","hw":{"rows":3,"cols":4,"enc":1}}';
  @override
  Future<Uint8List> readProfile() async => Uint8List(61);
  @override
  Future<Uint8List> readSequences() async => Uint8List(145 * 8);
  @override
  Future<void> writeProfile(Uint8List bytes) async {}
  @override
  Future<void> writeControl(Uint8List bytes) async {}
  @override
  Stream<bool> connectionState(String address) async* {
    yield true;
  }
  @override
  Future<void> disconnect(String address) async {}
}