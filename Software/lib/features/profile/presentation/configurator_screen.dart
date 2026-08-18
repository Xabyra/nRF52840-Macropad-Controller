import 'package:flutter/material.dart';
import 'dart:typed_data';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:universal_ble/universal_ble.dart';

import '../../../app_providers.dart';
import '../domain/action_data.dart';
import '../domain/macropad_profile.dart';
import 'sequence_manager_screen.dart';

class ConfiguratorScreen extends ConsumerStatefulWidget {
  const ConfiguratorScreen({super.key});

  @override
  ConsumerState<ConfiguratorScreen> createState() => _ConfiguratorScreenState();
}

class _ConfiguratorScreenState extends ConsumerState<ConfiguratorScreen> {
  final _nameController = TextEditingController();

  @override
  void dispose() {
    _nameController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // Слухаємо стан підключення. Якщо пристрій відключається, повертаємось на попередній екран.
    ref.listen<BleDevice?>(connectedDeviceProvider, (previous, next) {
      if (previous != null && next == null && mounted) {
        Navigator.of(context).pop();
      }
    });

    final hw = ref.watch(hardwareDescriptorProvider);
    final profilesList = ref.watch(profilesListProvider);
    final activeIndex = ref.watch(activeProfileIndexProvider);
    final editingIndex = ref.watch(editingProfileIndexProvider);

    if (hw == null || profilesList == null || profilesList.isEmpty) {
      // Поки дані завантажуються, показуємо індикатор завантаження.
      // Завдяки слухачу вище, якщо станеться помилка, екран автоматично закриється.
      return Scaffold(
        appBar: AppBar(title: const Text('Завантаження...')),
        body: const Center(child: CircularProgressIndicator()),
      );
    }

    final safeEditingIndex = editingIndex < profilesList.length ? editingIndex : 0;
    final profile = profilesList[safeEditingIndex];

    // Оновлюємо текст в контролері, тільки якщо він змінився,
    // щоб не втрачати позицію курсора.
    if (_nameController.text != profile.name) {
      _nameController.text = profile.name;
    }

    return PopScope(
      canPop: false, // Блокуємо системну кнопку/жест "назад"
      child: Scaffold(
        appBar: AppBar(
          automaticallyImplyLeading: false, // Прибираємо візуальну стрілку "назад" з AppBar
          title: const Text('Конфігуратор'),
        ),
        body: Padding(
          padding: const EdgeInsets.all(12.0),
          child: Column( // Виправлено: конструктор Column повинен бути закритий
          children: [
            Row(
              children: [
                const Text('Редагувати: '),
                const SizedBox(width: 8),
                DropdownButton<int>(
                  value: safeEditingIndex,
                  items: List.generate(profilesList.length, (index) {
                    return DropdownMenuItem(
                      value: index,
                      child: Text('Профіль ${index + 1} ${activeIndex == index ? "(Активний)" : ""}'),
                    );
                  }),
                  onChanged: (value) {
                    if (value != null) {
                      ref.read(editingProfileIndexProvider.notifier).state = value;
                    }
                  },
                ),
                const Spacer(),
                if (activeIndex != safeEditingIndex)
                  ElevatedButton(
                    onPressed: () {
                      final bytes = Uint8List.fromList([safeEditingIndex]);
                      ref.read(bleRepositoryProvider).writeProfile(bytes);
                      ref.read(activeProfileIndexProvider.notifier).state = safeEditingIndex;
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(content: Text('Профіль ${safeEditingIndex + 1} активовано!')),
                      );
                    },
                    child: const Text('Активувати'),
                  ),
              ],
            ),
            const SizedBox(height: 16),
            TextField(
              controller: _nameController,
              decoration: const InputDecoration(
                labelText: 'Назва профілю',
              ),
              onChanged: (value) {
                final updatedList = List<MacropadProfile>.from(profilesList);
                updatedList[safeEditingIndex] = profile.copyWith(name: value);
                ref.read(profilesListProvider.notifier).state = updatedList;
              },
            ),
            const SizedBox(height: 16),
            Expanded(  
              child: LayoutBuilder(
                builder: (context, constraints) {
                  const crossAxisSpacing = 8.0;
                  const mainAxisSpacing = 8.0;
                  // Розраховуємо розміри елементів, щоб вони заповнили простір
                  final itemWidth = (constraints.maxWidth - (hw.cols - 1) * crossAxisSpacing) / hw.cols;
                  final itemHeight = (constraints.maxHeight - (hw.rows - 1) * mainAxisSpacing) / hw.rows;
                  // Запобігаємо помилці ділення на нуль, якщо висота ще не визначена
                  final childAspectRatio = itemHeight > 0 ? itemWidth / itemHeight : 1.0;

                  return GridView.builder(
                    physics: const NeverScrollableScrollPhysics(), // Забороняємо скрол
                    gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                      crossAxisCount: hw.cols,
                      crossAxisSpacing: crossAxisSpacing,
                      mainAxisSpacing: mainAxisSpacing,
                      childAspectRatio: childAspectRatio,
                    ),
                    itemCount: hw.rows * hw.cols,
                    itemBuilder: (context, index) {
                      final action = profile.matrix[index];
                      return ElevatedButton(
                        style: ElevatedButton.styleFrom(padding: const EdgeInsets.all(4)),
                        onPressed: () => _showActionEditor(context, ref, index),
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Text('K${index + 1}', style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
                            const SizedBox(height: 4),
                            Text(
                              _formatAction(action),
                              textAlign: TextAlign.center,
                              style: TextStyle(
                                fontSize: 12,
                                color: action.type == 0 ? Theme.of(context).disabledColor : null,
                              ),
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ],
                        ),
                      );
                    },
                  );
                },
              ),
            ),
            if (hw.enc > 0) ...[
              const SizedBox(height: 16),
              const Text('Енкодер 1', style: TextStyle(fontWeight: FontWeight.bold)),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  _buildEncoderButton(context, ref, 'Вліво', profile.encCcw, -3),
                  _buildEncoderButton(context, ref, 'Кнопка', profile.encBtn, -1),
                  _buildEncoderButton(context, ref, 'Вправо', profile.encCw, -2),
                ],
              ),
            ],
            if (hw.led > 0) ...[
              const SizedBox(height: 16),
              _buildLiveLedControls(context, ref),
            ],
            const SizedBox(height: 16),
            _buildSleepTimerCard(ref),
            const SizedBox(height: 16),
            ElevatedButton.icon(
              onPressed: () {
                Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const SequenceManagerScreen()),
                );
              },
              icon: const Icon(Icons.timeline),
              label: const Text('Макроси / послідовності'),
              style: ElevatedButton.styleFrom(
                minimumSize: const Size(double.infinity, 48),
              ),
            ),
            const SizedBox(height: 16),
            ElevatedButton.icon(
              onPressed: () {
                final profileBytes = profile.toBytes();
                final payload = Uint8List(MacropadProfile.binarySize + 1);
                payload[0] = safeEditingIndex;
                payload.setRange(1, MacropadProfile.binarySize + 1, profileBytes);
                ref.read(bleRepositoryProvider).writeProfile(payload);
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(content: Text('Профіль ${safeEditingIndex + 1} збережено на пристрій!')),
                );
              },
              icon: const Icon(Icons.save),
              label: const Text('Зберегти на пристрій'),
              style: ElevatedButton.styleFrom(
                minimumSize: const Size(double.infinity, 48),
              ),
            ),
          ],
        ),
      ),
    )
    );
  }

  // Примітка: Усі допоміжні методи (_buildLiveLedControls, _buildEncoderButton, 
  // _formatAction, _showActionEditor) тепер є частиною класу _ConfiguratorScreenState 
  // і повинні бути розміщені тут.
  Widget _buildLiveLedControls(BuildContext context, WidgetRef ref) {
    final ledOn = ref.watch(ledOnProvider);
    final ledVal = ref.watch(ledValProvider);
    final safeLedVal = ledVal.toDouble().clamp(10.0, 100.0); // Захист від збою Slider

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text('Підсвітка (Live Control)', style: TextStyle(fontWeight: FontWeight.bold)),
                Switch(
                  value: ledOn,
                  onChanged: (val) {
                    ref.read(ledOnProvider.notifier).state = val;
                    ref.read(bleRepositoryProvider).writeProfile(Uint8List.fromList([0xAA, val ? safeLedVal.toInt() : 0]));
                  },
                ),
              ],
            ),
            const SizedBox(height: 8),
            Text('Яскравість: ${safeLedVal.toInt()}%'),
            Slider(
              value: safeLedVal,
              min: 10,
              max: 100,
              divisions: 9,
              label: '${safeLedVal.toInt()}%',
              onChanged: ledOn ? (value) {
                final intVal = value.toInt();
                ref.read(ledValProvider.notifier).state = intVal;
                ref.read(bleRepositoryProvider).writeProfile(Uint8List.fromList([0xAA, intVal]));
              } : null,
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSleepTimerCard(WidgetRef ref) {
    final sleepMin = ref.watch(sleepMinProvider);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('Таймер сну', style: TextStyle(fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            Text(sleepMin == 0 ? 'Сон вимкнено' : 'Сон через $sleepMin хвилин'),
            Slider(
              value: sleepMin.toDouble(),
              min: 0,
              max: 120,
              divisions: 120,
              label: sleepMin == 0 ? 'Вимкнено' : '$sleepMin хв',
              onChanged: (value) {
                final minutes = value.toInt();
                ref.read(sleepMinProvider.notifier).state = minutes;
                ref.read(bleRepositoryProvider).writeControl(Uint8List.fromList([0xAB, minutes]));
              },
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildEncoderButton(BuildContext context, WidgetRef ref, String title, ActionData action, int actionIndex) {
    return Expanded(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4.0),
        child: ElevatedButton(
          style: ElevatedButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 4)),
          onPressed: () => _showActionEditor(context, ref, actionIndex),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(title, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 12)),
              const SizedBox(height: 4),
              Text(
                _formatAction(action),
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 11,
                  color: action.type == 0 ? Theme.of(context).disabledColor : null,
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ),
    );
  }

  String _formatAction(ActionData action) {
    if (action.type == 0) return '—';

    if (action.type == 1) {
      List<String> parts = [];
      if ((action.mod & 0x01) != 0) parts.add('Ctrl');
      if ((action.mod & 0x02) != 0) parts.add('Shift');
      if ((action.mod & 0x04) != 0) parts.add('Alt');
      if ((action.mod & 0x08) != 0) parts.add('Win');

      String keyName = 'Код ${action.code}';
      if (action.code >= 4 && action.code <= 29) {
        keyName = String.fromCharCode(action.code - 4 + 65); // Літери A-Z
      } else if (action.code >= 30 && action.code <= 39) {
        keyName = ((action.code - 29) % 10).toString(); // Цифри 1-0
      } else if (action.code == 40) { keyName = 'Enter'; }
      else if (action.code == 41) { keyName = 'Esc'; }
      else if (action.code == 42) { keyName = 'Backspace'; }
      else if (action.code == 43) { keyName = 'Tab'; }
      else if (action.code == 44) { keyName = 'Space'; }

      if (action.code != 0 || parts.isEmpty) {
        parts.add(keyName);
      }
      return parts.join(' + ');
    }

    if (action.type == 2) {
      if (action.code == 1) return 'L-Click';
      if (action.code == 2) return 'R-Click';
      if (action.code == 3) return 'M-Click';
      if (action.code == 4) return 'Колесо Вгору';
      if (action.code == 5) return 'Колесо Вниз';
      return 'Mouse ${action.code}';
    }

    if (action.type == 4) {
      if (action.code == 1) return 'Світло +10%';
      if (action.code == 2) return 'Світло -10%';
      if (action.code == 3) return 'Світло Увімк/Вимк';
      return 'Світло (Невідомо)';
    }

    if (action.type == 5) {
      return 'Sequence ${action.code}';
    }

    return 'Невідомо';
  }

  String _keyboardKeyLabel(int code) {
    if (code == 0) return 'Немає клавіші';
    if (code >= 4 && code <= 29) {
      return String.fromCharCode(code - 4 + 65);
    }
    if (code >= 30 && code <= 39) {
      return ((code - 29) % 10).toString();
    }
    if (code >= 58 && code <= 69) {
      return 'F${code - 57}';
    }
    switch (code) {
      case 40:
        return 'Enter';
      case 41:
        return 'Esc';
      case 42:
        return 'Backspace';
      case 43:
        return 'Tab';
      case 44:
        return 'Space';
      default:
        return 'HID $code';
    }
  }

  bool _keyboardKeyIsValid(int code) {
    return code == 0 || (code >= 4 && code <= 29) || (code >= 30 && code <= 39) || (code >= 58 && code <= 69) || (code >= 40 && code <= 44);
  }

  void _showActionEditor(BuildContext context, WidgetRef ref, int actionIndex) {
    final profilesList = ref.read(profilesListProvider);
    if (profilesList == null) return;
    final editingIndex = ref.read(editingProfileIndexProvider);
    final profile = profilesList[editingIndex];

    ActionData currentAction;
    String title;

    if (actionIndex >= 0) {
      currentAction = profile.matrix[actionIndex];
      title = 'Key ${actionIndex + 1}';
    } else if (actionIndex == -1) {
      currentAction = profile.encBtn;
      title = 'Кнопки енкодера';
    } else if (actionIndex == -2) {
      currentAction = profile.encCw;
      title = 'Повороту вправо';
    } else {
      currentAction = profile.encCcw;
      title = 'Повороту вліво';
    }

    // Захист від сміттєвих даних в пам'яті для Dropdown типу дії
    int type = [0, 1, 2, 4, 5].contains(currentAction.type) ? currentAction.type : 0;
    int code = currentAction.code;
    int mod = currentAction.mod;

    showDialog(
      context: context,
      builder: (context) {
        return StatefulBuilder(
          builder: (context, setState) {
            return AlertDialog(
              title: Text('Дія для $title'),
              content: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    DropdownButtonFormField<int>(
                      value: type,
                      decoration: const InputDecoration(labelText: 'Тип дії'),
                      items: const [
                        DropdownMenuItem(value: 0, child: Text('Вимкнено (None)')),
                        DropdownMenuItem(value: 1, child: Text('Клавіатура (Key)')),
                        DropdownMenuItem(value: 2, child: Text('Миша (Mouse)')),
                        DropdownMenuItem(value: 4, child: Text('Підсвітка (Light)')),
                        DropdownMenuItem(value: 5, child: Text('Послідовність (Sequence)')),
                      ],
                      onChanged: (v) => setState(() {
                        type = v ?? 0;
                        if (type == 4 && ![1, 2, 3].contains(code)) {
                          code = 3; // Default до Toggle для підсвітки
                        }
                        if (type == 2 && ![1, 2, 3, 4, 5].contains(code)) {
                          code = 1; // Default до L-Click для миші
                        }
                        if (type == 5 && code > 7) {
                          code = 0;
                        }
                      }),
                    ),
                    if (type == 1) ...[
                      const SizedBox(height: 12),
                      const Text('Модифікатори:', style: TextStyle(fontSize: 12)),
                      Wrap(
                        spacing: 4,
                        children: [
                          FilterChip(label: const Text('Ctrl'), selected: (mod & 0x01) != 0, onSelected: (v) => setState(() => mod = v ? (mod | 0x01) : (mod & ~0x01))),
                          FilterChip(label: const Text('Shift'), selected: (mod & 0x02) != 0, onSelected: (v) => setState(() => mod = v ? (mod | 0x02) : (mod & ~0x02))),
                          FilterChip(label: const Text('Alt'), selected: (mod & 0x04) != 0, onSelected: (v) => setState(() => mod = v ? (mod | 0x04) : (mod & ~0x04))),
                          FilterChip(label: const Text('Win'), selected: (mod & 0x08) != 0, onSelected: (v) => setState(() => mod = v ? (mod | 0x08) : (mod & ~0x08))),
                        ],
                      ),
                    ],
                    if (type == 1) ...[
                      const SizedBox(height: 12),
                      DropdownButtonFormField<int>(
                        value: _keyboardKeyIsValid(code) ? code : 0,
                        decoration: const InputDecoration(labelText: 'Клавіша'),
                        items: [
                          const DropdownMenuItem(value: 0, child: Text('Немає клавіші')),
                          for (var code = 4; code <= 29; code++)
                            DropdownMenuItem(value: code, child: Text(_keyboardKeyLabel(code))),
                          for (var code = 30; code <= 39; code++)
                            DropdownMenuItem(value: code, child: Text(_keyboardKeyLabel(code))),
                          for (var code = 58; code <= 69; code++)
                            DropdownMenuItem(value: code, child: Text(_keyboardKeyLabel(code))),
                          const DropdownMenuItem(value: 40, child: Text('Enter')),
                          const DropdownMenuItem(value: 41, child: Text('Esc')),
                          const DropdownMenuItem(value: 42, child: Text('Backspace')),
                          const DropdownMenuItem(value: 43, child: Text('Tab')),
                          const DropdownMenuItem(value: 44, child: Text('Space')),
                        ],
                        onChanged: (v) => setState(() => code = v ?? 0),
                      ),
                    ],
                    if (type == 2) ...[
                      const SizedBox(height: 12),
                      DropdownButtonFormField<int>(
                        value: [1, 2, 3, 4, 5].contains(code) ? code : 1,
                        decoration: const InputDecoration(labelText: 'Дія миші'),
                        items: const [
                          DropdownMenuItem(value: 1, child: Text('Ліва клавіша (L-Click)')),
                          DropdownMenuItem(value: 2, child: Text('Права клавіша (R-Click)')),
                          DropdownMenuItem(value: 3, child: Text('Середня клавіша (M-Click)')),
                          DropdownMenuItem(value: 4, child: Text('Колесо вгору (Scroll Up)')),
                          DropdownMenuItem(value: 5, child: Text('Колесо вниз (Scroll Down)')),
                        ],
                        onChanged: (v) => setState(() => code = v ?? 1),
                      ),
                    ],
                    if (type == 4) ...[
                      const SizedBox(height: 12),
                      DropdownButtonFormField<int>(
                        value: [1, 2, 3].contains(code) ? code : 3,
                        decoration: const InputDecoration(labelText: 'Дія з підсвіткою'),
                        items: const [
                          DropdownMenuItem(value: 3, child: Text('Увімкнути/Вимкнути (Toggle)')),
                          DropdownMenuItem(value: 1, child: Text('Збільшити яскравість (+10%)')),
                          DropdownMenuItem(value: 2, child: Text('Зменшити яскравість (-10%)')),
                        ],
                        onChanged: (v) => setState(() => code = v ?? 3),
                      ),
                    ],
                    if (type == 5) ...[
                      const SizedBox(height: 12),
                      DropdownButtonFormField<int>(
                        value: (code >= 0 && code <= 7) ? code : 0,
                        decoration: const InputDecoration(labelText: 'Індекс послідовності'),
                        items: List.generate(8, (index) {
                          return DropdownMenuItem(value: index, child: Text('Sequence $index'));
                        }),
                        onChanged: (v) => setState(() => code = v ?? 0),
                      ),
                    ],
                  ],
                ),
              ),
              actions: [
                TextButton(onPressed: () => Navigator.pop(context), child: const Text('Скасувати')),
                ElevatedButton(
                  onPressed: () {
                    final newAction = ActionData(type: type, mod: mod, code: code);
                    MacropadProfile updatedProfile = profile;

                    if (actionIndex >= 0) {
                      final newMatrix = List<ActionData>.from(profile.matrix);
                      newMatrix[actionIndex] = newAction;
                      updatedProfile = profile.copyWith(matrix: newMatrix);
                    } else if (actionIndex == -1) {
                      updatedProfile = profile.copyWith(encBtn: newAction);
                    } else if (actionIndex == -2) {
                      updatedProfile = profile.copyWith(encCw: newAction);
                    } else {
                      updatedProfile = profile.copyWith(encCcw: newAction);
                    }

                    final updatedList = List<MacropadProfile>.from(profilesList);
                    updatedList[editingIndex] = updatedProfile;
                    ref.read(profilesListProvider.notifier).state = updatedList;
                    Navigator.pop(context);
                  },
                  child: const Text('Зберегти'),
                ),
              ],
            );
          },
        );
      },
    );
  }
}