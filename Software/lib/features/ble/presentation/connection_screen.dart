import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:universal_ble/universal_ble.dart';

import '../../../app_providers.dart';
import '../../profile/domain/hardware_descriptor.dart';
import '../../profile/domain/key_sequence.dart';
import '../../profile/domain/macropad_profile.dart';
import '../../profile/presentation/configurator_screen.dart';

class ConnectionScreen extends ConsumerStatefulWidget {
  const ConnectionScreen({super.key});

  @override
  ConsumerState<ConnectionScreen> createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends ConsumerState<ConnectionScreen> {
  StreamSubscription? _scanSubscription;

  @override
  void dispose() {
    _scanSubscription?.cancel();
    UniversalBle.stopScan();
    super.dispose();
  }

  void _startScan() {
    ref.read(isScanningProvider.notifier).state = true;
    ref.read(scanResultsProvider.notifier).state = []; // Очищуємо попередні результати

    final bleRepo = ref.read(bleRepositoryProvider);
    _scanSubscription = bleRepo.scanDevices().listen((results) {
      // Не фільтруємо результат сканування за UUID.
      // Показуємо всі знайдені пристрої і вибираємо потрібний за іменем.
      ref.read(scanResultsProvider.notifier).state = results.toList();
    });

    // Зупиняємо індикатор сканування після таймауту
    Future.delayed(const Duration(seconds: 5), () {
      if (mounted) {
        ref.read(isScanningProvider.notifier).state = false;
      }
    });
  }

  Future<void> _connectToDevice(BleDevice device) async {
    ref.read(connectionStatusProvider.notifier).state = 'Підключення до ${device.name ?? "Unknown"}...';
    final bleRepo = ref.read(bleRepositoryProvider);

    try {
      await bleRepo.connect(device.deviceId);
      ref.read(connectedDeviceProvider.notifier).state = device;
      ref.read(connectionStatusProvider.notifier).state = 'Підключено до ${device.name ?? "Unknown"}';

      // Отримуємо інформацію про пристрій та профіль
      final jsonStr = await bleRepo.readInfoJson();
      final hardware = HardwareDescriptor.fromJson(jsonStr);
      ref.read(hardwareDescriptorProvider.notifier).state = hardware;

      try {
        final decoded = json.decode(jsonStr);
        if (decoded['active_prof'] != null) {
          ref.read(activeProfileIndexProvider.notifier).state = decoded['active_prof'] as int;
        }
        if (decoded['led_on'] != null) {
          ref.read(ledOnProvider.notifier).state = (decoded['led_on'] as int) == 1;
        }
        if (decoded['led_val'] != null) {
          ref.read(ledValProvider.notifier).state = decoded['led_val'] as int;
        }
        if (decoded['sleep_min'] != null) {
          ref.read(sleepMinProvider.notifier).state = decoded['sleep_min'] as int;
        }
      } catch (_) {}

      try {
        // Зчитуємо Dump усіх профілів (183 байти)
        final dumpBytes = await bleRepo.readProfile();
        List<MacropadProfile> profiles = [];
        for (int i = 0; i < 3; i++) {
          final offset = i * MacropadProfile.binarySize;
          if (offset + MacropadProfile.binarySize <= dumpBytes.length) {
            final profileBytes = dumpBytes.sublist(offset, offset + MacropadProfile.binarySize);
            profiles.add(MacropadProfile.fromBytes(profileBytes, rows: hardware.rows, cols: hardware.cols));
          }
        }
        ref.read(profilesListProvider.notifier).state = profiles;
        final activeIndex = ref.read(activeProfileIndexProvider);
        if (profiles.isNotEmpty && activeIndex >= 0 && activeIndex < profiles.length) {
          ref.read(profileProvider.notifier).state = profiles[activeIndex];
        }
      } catch (e) {
        // Якщо не вдалося зчитати, створюємо дефолтні
        final defaultProfile = MacropadProfile.empty(rows: hardware.rows, cols: hardware.cols);
        ref.read(profilesListProvider.notifier).state = [defaultProfile, defaultProfile, defaultProfile];
        ref.read(profileProvider.notifier).state = defaultProfile;
      }

      try {
        final sequenceBytes = await bleRepo.readSequences();
        final sequences = KeySequence.listFromBytes(Uint8List.fromList(sequenceBytes));
        if (sequences.isEmpty) {
          ref.read(sequencesListProvider.notifier).state = List.generate(8, (_) => KeySequence.empty());
        } else {
          ref.read(sequencesListProvider.notifier).state = sequences;
        }
      } catch (_) {
        ref.read(sequencesListProvider.notifier).state = List.generate(8, (_) => KeySequence.empty());
      }

      // Переходимо на екран конфігуратора після успішного підключення та зчитування
      if (mounted) {
        await Navigator.push(
          context,
          MaterialPageRoute(builder: (context) => const ConfiguratorScreen()),
        );
      }

    } catch (e) {
      final errorMessage = 'Помилка: ${e.toString()}';

      // Відключення та очищення стану тепер повністю обробляється всередині методу `connect`
      // в репозиторії, тому дублювати логіку відключення тут не потрібно.

      ref.read(connectedDeviceProvider.notifier).state = null;
      ref.read(hardwareDescriptorProvider.notifier).state = null;
      ref.read(profilesListProvider.notifier).state = null;
      ref.read(connectionStatusProvider.notifier).state = errorMessage;
    }
  }

  @override
  Widget build(BuildContext context) {
    final scanResults = ref.watch(scanResultsProvider);
    final isScanning = ref.watch(isScanningProvider);
    final connectedDevice = ref.watch(connectedDeviceProvider);
    final status = ref.watch(connectionStatusProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Підключення'),
        actions: [
          if (isScanning)
            const Padding(
              padding: EdgeInsets.only(right: 16.0),
              child: SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2)),
            ),
        ],
      ),
      body: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: Text(status, style: Theme.of(context).textTheme.titleMedium, textAlign: TextAlign.center),
          ),
          Expanded(
            child: scanResults.isEmpty && !isScanning
                ? Center(
                    child: Text(
                      'Пристроїв не знайдено.\nНатисніть "Шукати", щоб почати сканування.',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodyLarge,
                    ),
                  )
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    itemCount: scanResults.length,
                    itemBuilder: (context, index) {
                      final result = scanResults[index];
                      return Card(
                        margin: const EdgeInsets.symmetric(vertical: 6),
                        child: ListTile(
                          leading: const CircleAvatar(child: Icon(Icons.memory)), // Іконка макропада
                          title: Text(result.name ?? 'Unknown Device'),
                          subtitle: Text(result.deviceId),
                          trailing: const Icon(Icons.arrow_forward_ios, size: 16),
                          onTap: connectedDevice != null ? null : () => _connectToDevice(result),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: isScanning ? null : _startScan,
        label: const Text('Шукати пристрої'),
        icon: const Icon(Icons.search),
      ),
    );
  }
}