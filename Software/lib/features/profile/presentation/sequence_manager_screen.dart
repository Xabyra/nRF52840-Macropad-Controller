import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../app_providers.dart';
import '../domain/key_sequence.dart';

class SequenceManagerScreen extends ConsumerStatefulWidget {
  const SequenceManagerScreen({super.key});

  @override
  ConsumerState<SequenceManagerScreen> createState() => _SequenceManagerScreenState();
}

class _SequenceManagerScreenState extends ConsumerState<SequenceManagerScreen> {
  @override
  Widget build(BuildContext context) {
    final sequences = ref.watch(sequencesListProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Макроси / Послідовності'),
      ),
      body: sequences == null
          ? const Center(child: CircularProgressIndicator())
          : ListView.separated(
              padding: const EdgeInsets.all(12),
              itemCount: sequences.length,
              separatorBuilder: (_, __) => const SizedBox(height: 12),
              itemBuilder: (context, index) {
                final sequence = sequences[index];
                return Card(
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                  elevation: 3,
                  child: Padding(
                    padding: const EdgeInsets.all(16.0),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text('Sequence $index', style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
                        const SizedBox(height: 8),
                        Text('Назва: ${sequence.name}', style: Theme.of(context).textTheme.bodyLarge),
                        const SizedBox(height: 4),
                        Text('Кроків: ${sequence.len}', style: Theme.of(context).textTheme.bodyMedium),
                        const SizedBox(height: 4),
                        Text(
                          sequence.randomDelayMax == 0
                              ? 'Random: OFF'
                              : 'Random: 0-${sequence.randomDelayMax} ms',
                          style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                                color: sequence.randomDelayMax == 0 ? Theme.of(context).disabledColor : Theme.of(context).colorScheme.primary,
                              ),
                        ),
                        const SizedBox(height: 12),
                        Wrap(
                          spacing: 8,
                          runSpacing: 8,
                          children: [
                            ElevatedButton(
                              onPressed: () => _sendSequence(index),
                              child: const Text('Запустити'),
                            ),
                            ElevatedButton(
                              onPressed: () => _showSequenceEditor(index, sequence),
                              child: const Text('Редагувати'),
                            ),
                            TextButton(
                              onPressed: () => _clearSequence(index),
                              child: const Text('Очистити'),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                );
              },
            ),
    );
  }

  Future<void> _sendSequence(int index) async {
    try {
      await ref.read(bleRepositoryProvider).writeControl(Uint8List.fromList([0xB0, index]));
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Sequence $index відправлено на пристрій')));
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Не вдалося запустити Sequence $index: $e')));
      }
    }
  }

  Future<void> _clearSequence(int index) async {
    try {
      await ref.read(bleRepositoryProvider).writeControl(Uint8List.fromList([0xB2, index, 1]));
      final updated = List<KeySequence>.from(ref.read(sequencesListProvider) ?? []);
      if (index >= 0 && index < updated.length) {
        updated[index] = const KeySequence(name: 'Sequence', len: 0, randomDelayMax: 0, steps: []);
        ref.read(sequencesListProvider.notifier).state = updated;
      }
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Sequence $index очищено')));
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Не вдалося очистити Sequence $index: $e')));
      }
    }
  }

  void _showSequenceEditor(int index, KeySequence sequence) {
    final nameController = TextEditingController(text: sequence.name);
    final randomDelayController = TextEditingController(text: sequence.randomDelayMax.toString());
    final steps = sequence.steps.toList();
    var randomDelayMax = sequence.randomDelayMax;

    final delayControllers = <TextEditingController>[];
    for (var i = 0; i < steps.length; i++) {
      delayControllers.add(TextEditingController(text: steps[i].delayMs.toString()));
    }

    showDialog(
      context: context,
      builder: (context) {
        return StatefulBuilder(
          builder: (context, setState) {
            return AlertDialog(
              title: Text('Sequence $index'),
              content: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    TextField(
                      controller: nameController,
                      decoration: const InputDecoration(labelText: 'Назва'),
                    ),
                    const SizedBox(height: 16),
                    Text('Random Delay Max (ms)', style: Theme.of(context).textTheme.bodyLarge),
                    const SizedBox(height: 6),
                    Row(
                      children: [
                        Expanded(
                          child: Slider(
                            value: randomDelayMax.toDouble(),
                            min: 0,
                            max: 5000,
                            divisions: 100,
                            label: randomDelayMax == 0 ? 'OFF' : '$randomDelayMax ms',
                            onChanged: (value) {
                              setState(() {
                                randomDelayMax = value.round();
                                randomDelayController.text = randomDelayMax.toString();
                              });
                            },
                          ),
                        ),
                        const SizedBox(width: 8),
                        SizedBox(
                          width: 90,
                          child: TextField(
                            controller: randomDelayController,
                            decoration: const InputDecoration(suffixText: 'ms'),
                            keyboardType: TextInputType.number,
                            onChanged: (value) {
                              final parsed = int.tryParse(value) ?? 0;
                              setState(() {
                                randomDelayMax = parsed.clamp(0, 5000);
                              });
                            },
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 4),
                    Text(
                      randomDelayMax == 0
                          ? 'Random: OFF'
                          : 'Random: 0-$randomDelayMax ms',
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                            color: randomDelayMax == 0 ? Theme.of(context).disabledColor : Theme.of(context).colorScheme.primary,
                          ),
                    ),
                    const SizedBox(height: 12),
                    Text('Final Delay = Base Delay + Random(0..$randomDelayMax)', style: Theme.of(context).textTheme.bodySmall),
                    const SizedBox(height: 16),
                    if (steps.isNotEmpty) ...[
                      for (var stepIndex = 0; stepIndex < steps.length; stepIndex++)
                        _buildStepEditor(
                          stepIndex,
                          steps[stepIndex],
                          randomDelayMax,
                          (updated) => setState(() => steps[stepIndex] = updated),
                          delayControllers[stepIndex],
                        ),
                      const SizedBox(height: 8),
                    ],
                    if (steps.isEmpty) const Text('Немає кроків. Додайте новий крок.'),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        ElevatedButton(
                          onPressed: steps.length < KeySequence.maxSteps
                              ? () => setState(() {
                                    steps.add(const SequenceStep(mod: 0, code: 0, delayMs: 0));
                                    delayControllers.add(TextEditingController(text: '0'));
                                  })
                              : null,
                          child: const Text('Додати крок'),
                        ),
                        const SizedBox(width: 8),
                        Text('${steps.length}/${KeySequence.maxSteps}'),
                      ],
                    ),
                  ],
                ),
              ),
              actions: [
                TextButton(onPressed: () => Navigator.pop(context), child: const Text('Скасувати')),
                ElevatedButton(
                  onPressed: () async {
                    final updatedSequence = KeySequence(
                      name: nameController.text.isEmpty ? 'Sequence' : nameController.text,
                      len: steps.length.clamp(0, KeySequence.maxSteps),
                      randomDelayMax: randomDelayMax,
                      steps: steps,
                    );
                    await _writeSequence(index, updatedSequence);
                    if (context.mounted) {
                      Navigator.pop(context);
                    }
                  },
                  child: const Text('Записати на пристрій'),
                ),
              ],
            );
          },
        );
      },
    ).then((_) {
      nameController.dispose();
      randomDelayController.dispose();
      for (final controller in delayControllers) {
        controller.dispose();
      }
    });
  }

  Widget _buildStepEditor(int stepIndex, SequenceStep step, int randomDelayMax, void Function(SequenceStep) onChanged, TextEditingController delayController) {
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 6),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
      elevation: 1,
      child: Padding(
        padding: const EdgeInsets.all(12.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Крок ${stepIndex + 1}', style: const TextStyle(fontWeight: FontWeight.bold)),
            const SizedBox(height: 12),
            Text('Модифікатори', style: Theme.of(context).textTheme.bodyMedium),
            const SizedBox(height: 8),
            Wrap(
              spacing: 6,
              children: [
                FilterChip(
                  label: const Text('Ctrl'),
                  selected: (step.mod & 0x01) != 0,
                  onSelected: (value) {
                    final newMod = value ? (step.mod | 0x01) : (step.mod & ~0x01);
                    onChanged(SequenceStep(mod: newMod, code: step.code, delayMs: step.delayMs));
                  },
                ),
                FilterChip(
                  label: const Text('Shift'),
                  selected: (step.mod & 0x02) != 0,
                  onSelected: (value) {
                    final newMod = value ? (step.mod | 0x02) : (step.mod & ~0x02);
                    onChanged(SequenceStep(mod: newMod, code: step.code, delayMs: step.delayMs));
                  },
                ),
                FilterChip(
                  label: const Text('Alt'),
                  selected: (step.mod & 0x04) != 0,
                  onSelected: (value) {
                    final newMod = value ? (step.mod | 0x04) : (step.mod & ~0x04);
                    onChanged(SequenceStep(mod: newMod, code: step.code, delayMs: step.delayMs));
                  },
                ),
                FilterChip(
                  label: const Text('Win'),
                  selected: (step.mod & 0x08) != 0,
                  onSelected: (value) {
                    final newMod = value ? (step.mod | 0x08) : (step.mod & ~0x08);
                    onChanged(SequenceStep(mod: newMod, code: step.code, delayMs: step.delayMs));
                  },
                ),
              ],
            ),
            const SizedBox(height: 12),
            DropdownButtonFormField<int>(
              value: step.code,
              decoration: const InputDecoration(labelText: 'Клавіша'),
              items: [
                const DropdownMenuItem(value: 0, child: Text('Немає клавіші')),
                for (var code = 4; code <= 29; code++)
                  DropdownMenuItem(value: code, child: Text(_keyCodeLabel(code))),
                for (var code = 30; code <= 39; code++)
                  DropdownMenuItem(value: code, child: Text(_keyCodeLabel(code))),
                for (var code = 58; code <= 69; code++)
                  DropdownMenuItem(value: code, child: Text(_keyCodeLabel(code))),
                const DropdownMenuItem(value: 40, child: Text('Enter')),
                const DropdownMenuItem(value: 41, child: Text('Esc')),
                const DropdownMenuItem(value: 42, child: Text('Backspace')),
                const DropdownMenuItem(value: 43, child: Text('Tab')),
                const DropdownMenuItem(value: 44, child: Text('Space')),
              ],
              onChanged: (value) => onChanged(SequenceStep(mod: step.mod, code: value ?? 0, delayMs: step.delayMs)),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: delayController,
              decoration: const InputDecoration(labelText: 'delay_ms'),
              keyboardType: TextInputType.number,
              onChanged: (value) {
                final delay = int.tryParse(value) ?? 0;
                onChanged(SequenceStep(mod: step.mod, code: step.code, delayMs: delay));
              },
            ),
            const SizedBox(height: 8),
            Text(
              randomDelayMax == 0
                  ? 'Final Delay = ${step.delayMs} ms'
                  : 'Preview: ${step.delayMs} + Random(0..$randomDelayMax) ms',
              style: Theme.of(context).textTheme.bodySmall?.copyWith(color: Theme.of(context).disabledColor),
            ),
          ],
        ),
      ),
    );
  }

  String _keyCodeLabel(int code) {
    if (code >= 4 && code <= 29) {
      return String.fromCharCode(code - 4 + 65);
    }
    if (code >= 30 && code <= 39) {
      return ((code - 29) % 10).toString();
    }
    switch (code) {
      case 0:
        return 'Немає клавіші';
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
        if (code >= 58 && code <= 69) {
          return 'F${code - 57}';
        }
        return 'HID $code';
    }
  }

  Future<void> _writeSequence(int index, KeySequence sequence) async {
    try {
      final payload = Uint8List(2 + KeySequence.binarySize);
      payload[0] = 0xB1;
      payload[1] = index;
      payload.setRange(2, 2 + KeySequence.binarySize, sequence.toBytes());
      await ref.read(bleRepositoryProvider).writeControl(payload);
      final current = ref.read(sequencesListProvider) ?? List.generate(8, (_) => KeySequence.empty());
      final updated = List<KeySequence>.from(current);
      if (index >= 0 && index < updated.length) {
        updated[index] = sequence;
      }
      ref.read(sequencesListProvider.notifier).state = updated;
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Sequence $index записано на пристрій')));
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('Не вдалося записати Sequence $index: $e')));
      }
    }
  }
}
