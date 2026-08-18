import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:universal_ble/universal_ble.dart';
import 'ble_repository.dart';

class UniversalBleRepository implements BleRepository {
  String? _connectedAddress;

  // Service and Characteristic UUIDs from your spec
  static const String _serviceUuid = "12345678-1234-5678-1234-56789abcdef0";
  static const String _infoCharUuid = "12345678-1234-5678-1234-56789abcdef1";
  static const String _dumpCharUuid = "12345678-1234-5678-1234-56789abcdef2";
  static const String _ctrlCharUuid = "12345678-1234-5678-1234-56789abcdef3";
  static const String _sequencesCharUuid = "12345678-1234-5678-1234-56789abcdef4";
  static const String _exportCharUuid = "12345678-1234-5678-1234-56789abcdef5";

  @override
  Stream<List<BleDevice>> scanDevices({Duration timeout = const Duration(seconds: 5)}) {
    final controller = StreamController<List<BleDevice>>();
    final devices = <String, BleDevice>{};

    UniversalBle.onScanResult = (BleDevice device) {
      if (device.name != null && device.name!.isNotEmpty) {
        devices[device.deviceId] = device;
        controller.add(devices.values.toList());
      }
    };

    UniversalBle.startScan();

    Future.delayed(timeout, () {
      UniversalBle.stopScan();
      controller.close();
    });

    return controller.stream;
  }

  @override
  Future<void> connect(String address) async {
    UniversalBle.stopScan();

    // Крок 0: "Жорстке" очищення попереднього стану (Запобіжник від помилки 19).
    // Змушує ОС закрити будь-які "завислі" GATT-сесії для цього пристрою перед новим підключенням.
    try {
      await UniversalBle.disconnect(address).timeout(const Duration(seconds: 2));
      await Future.delayed(const Duration(milliseconds: 500));
    } catch (_) {}

    try {
      // Крок 1: Встановлення нового з'єднання
      await UniversalBle.connect(address).timeout(const Duration(seconds: 15));
      _connectedAddress = address;

      // ОБОВ'ЯЗКОВО: Чекаємо перед викликом discoverServices!
      // Фізично пристрій міг підключитись, але ОС (особливо Android) потрібен час
      // на підготовку внутрішнього GATT-клієнта. Без цього буде помилка Service not found (19).
      await Future.delayed(const Duration(milliseconds: 1500));

      // Крок 2: Виявлення сервісів з автоматичним Retry-механізмом
      try {
        await UniversalBle.discoverServices(address).timeout(const Duration(seconds: 10));
      } catch (e) {
        debugPrint('Перша спроба виявлення сервісів невдала ($e). Робимо повторну спробу...');
        await Future.delayed(const Duration(milliseconds: 1000));
        await UniversalBle.discoverServices(address).timeout(const Duration(seconds: 10));
      }

      // Фінальна пауза для повного оновлення кешів ОС перед першим читанням/записом
      await Future.delayed(const Duration(milliseconds: 800));
    } catch (e) {
      debugPrint('Помилка підключення або виявлення сервісів: $e');
      // Якщо на будь-якому етапі сталася помилка (включно з serviceNotFound),
      // ми примусово розриваємо з'єднання, щоб очистити стан перед наступною спробою.
      await disconnect(address);
      rethrow;
    }
  }

  @override
  Future<void> disconnect(String address) async {
    _connectedAddress = null; // Одразу блокуємо будь-які запити на зчитування/запис
    try {
      await UniversalBle.disconnect(address).timeout(const Duration(seconds: 5));
    } catch (_) {}
  }

  @override
  Stream<bool> connectionState(String address) {
    // Спрощена імплементація для universal_ble
    return Stream.value(true);
  }

  @override
  Future<String> readInfoJson() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    // Пряме читання без повторних спроб. Якщо `connect` відпрацював, все має бути готово.
    final value = await UniversalBle.readValue(_connectedAddress!, _serviceUuid, _infoCharUuid).timeout(const Duration(seconds: 5));
    return utf8.decode(value);
  }

  @override
  Future<Uint8List> readProfile() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    return Uint8List.fromList(await UniversalBle.readValue(_connectedAddress!, _serviceUuid, _dumpCharUuid).timeout(const Duration(seconds: 5)));
  }

  @override
  Future<void> writeProfile(Uint8List bytes) async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    await UniversalBle.writeValue(_connectedAddress!, _serviceUuid, _ctrlCharUuid, bytes, BleOutputProperty.withResponse).timeout(const Duration(seconds: 5));
  }

  @override
  Future<Uint8List> readSequences() async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    return Uint8List.fromList(await UniversalBle.readValue(_connectedAddress!, _serviceUuid, _sequencesCharUuid).timeout(const Duration(seconds: 5)));
  }

  @override
  Future<void> writeControl(Uint8List bytes) async {
    if (_connectedAddress == null) throw Exception('Не підключено');
    await UniversalBle.writeValue(_connectedAddress!, _serviceUuid, _ctrlCharUuid, bytes, BleOutputProperty.withResponse).timeout(const Duration(seconds: 5));
  }
}
