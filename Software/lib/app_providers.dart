import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:universal_ble/universal_ble.dart';

import 'features/ble/data/flutter_blue_repository.dart';
import 'features/ble/data/ble_repository.dart';
import 'features/profile/domain/hardware_descriptor.dart';
import 'features/profile/domain/key_sequence.dart';
import 'features/profile/domain/macropad_profile.dart';

/// Провайдер для BLE репозиторію.
/// Зараз використовує Mock, але в майбутньому тут можна буде підставити
/// реальну імплементацію для Android/Desktop.
final bleRepositoryProvider = Provider<BleRepository>((ref) {
  // Використовуємо універсальну імплементацію
  return UniversalBleRepository();
});

/// Провайдер для стану дескриптора "заліза".
final hardwareDescriptorProvider = StateProvider<HardwareDescriptor?>((ref) => null);

/// Провайдер для стану поточного профілю макропада.
final profileProvider = StateProvider<MacropadProfile?>((ref) => null);

/// Провайдери для стану UI підключення
final scanResultsProvider = StateProvider<List<BleDevice>>((ref) => []);
final isScanningProvider = StateProvider<bool>((ref) => false);
final connectedDeviceProvider = StateProvider<BleDevice?>((ref) => null);
final connectionStatusProvider = StateProvider<String>((ref) => "Не підключено");

/// Провайдери для стану підсвітки (Live Control)
final ledOnProvider = StateProvider<bool>((ref) => false);
final ledValProvider = StateProvider<int>((ref) => 70);

/// Провайдер для списку всіх профілів
final profilesListProvider = StateProvider<List<MacropadProfile>?>((ref) => null);

/// Провайдер для індексу активного профілю на пристрої
final activeProfileIndexProvider = StateProvider<int>((ref) => 0);

/// Провайдер для списку всіх макрос-послідовностей на пристрої
final sequencesListProvider = StateProvider<List<KeySequence>?>((ref) => null);

/// Провайдер для значення таймера сну
final sleepMinProvider = StateProvider<int>((ref) => 0);

/// Провайдер для індексу профілю, що редагується в UI
final editingProfileIndexProvider = StateProvider<int>((ref) => 0);

/// Утиліта для відключення від пристрою та скидання стану
Future<void> disconnectAndReset(WidgetRef ref) async {
  final bleRepo = ref.read(bleRepositoryProvider);
  final device = ref.read(connectedDeviceProvider);

  // 1. ПРИМУСОВО відключаємо BLE перед тим, як очистити стан UI.
  // Це гарантує, що при поверненні на екран пошуку пристрій вже вільний від підключень.
  if (device != null) {
    try {
      // Делегуємо відключення репозиторію. Він сам обробляє внутрішні таймаути.
      await bleRepo.disconnect(device.deviceId);
      await Future.delayed(const Duration(milliseconds: 1000)); // Даємо ОС час на оновлення GATT
    } catch (_) {
      // Ігноруємо помилки при відключенні, оскільки ми все одно скидаємо стан UI.
    }
  }

  // 2. Тепер безпечно скидаємо інтерфейс
  ref.read(connectedDeviceProvider.notifier).state = null;
  ref.read(connectionStatusProvider.notifier).state = "Відключено";
  ref.read(hardwareDescriptorProvider.notifier).state = null;
  ref.read(profileProvider.notifier).state = null;
  ref.read(profilesListProvider.notifier).state = null;
  ref.read(activeProfileIndexProvider.notifier).state = 0;
  ref.read(editingProfileIndexProvider.notifier).state = 0;
  ref.read(ledOnProvider.notifier).state = false;
  ref.read(ledValProvider.notifier).state = 70;
}