import 'dart:convert';
import 'dart:typed_data';

class SequenceStep {
  final int mod;
  final int code;
  final int delayMs;

  const SequenceStep({required this.mod, required this.code, required this.delayMs});

  factory SequenceStep.none() => const SequenceStep(mod: 0, code: 0, delayMs: 0);

  Uint8List toBytes() {
    final bytes = ByteData(4);
    bytes.setUint8(0, mod);
    bytes.setUint8(1, code);
    bytes.setUint16(2, delayMs, Endian.little);
    return bytes.buffer.asUint8List();
  }

  factory SequenceStep.fromBytes(Uint8List bytes, {int offset = 0}) {
    final view = ByteData.sublistView(bytes, offset, offset + 4);
    return SequenceStep(
      mod: view.getUint8(0),
      code: view.getUint8(1),
      delayMs: view.getUint16(2, Endian.little),
    );
  }

  Map<String, dynamic> toJson() => {
        'mod': mod,
        'code': code,
        'delay_ms': delayMs,
      };

  factory SequenceStep.fromJson(List<dynamic> json) {
    return SequenceStep(
      mod: (json[0] as num).toInt(),
      code: (json[1] as num).toInt(),
      delayMs: (json[2] as num).toInt(),
    );
  }
}

class KeySequence {
  static const int nameSize = 16;
  static const int maxSteps = 32;
  static const int binarySize = nameSize + 1 + maxSteps * 4;

  final String name;
  final int len;
  final int randomDelayMax;
  final List<SequenceStep> steps;

  const KeySequence({required this.name, required this.len, required this.randomDelayMax, required this.steps});

  factory KeySequence.empty() => const KeySequence(name: 'Sequence', len: 0, randomDelayMax: 0, steps: []);

  factory KeySequence.fromBytes(Uint8List bytes) {
    if (bytes.length != binarySize) {
      throw ArgumentError('Sequence must be exactly $binarySize bytes');
    }

    final nameBytes = bytes.sublist(0, nameSize);
    final name = utf8.decode(nameBytes.where((b) => b != 0).toList(), allowMalformed: true);
    final rawLen = (bytes[16] as int).clamp(0, maxSteps);

    final offset = 17;
    final rawSteps = <SequenceStep>[];
    for (var i = 0; i < rawLen; i++) {
      rawSteps.add(SequenceStep.fromBytes(bytes, offset: offset + i * 4));
    }

    // Detect special header: Alt + F15 (mod 0x04, code 72)
    var randomMax = 0;
    var steps = rawSteps;
    if (rawSteps.isNotEmpty) {
      final first = rawSteps[0];
      if (first.mod == 0x04 && first.code == 72) {
        randomMax = first.delayMs;
        // playable steps exclude the header
        steps = rawSteps.sublist(1);
      }
    }

    return KeySequence(
      name: name.isEmpty ? 'Sequence' : name,
      len: steps.length,
      randomDelayMax: randomMax,
      steps: steps,
    );
  }

  Uint8List toBytes() {
    final result = Uint8List(binarySize);
    final nameBytes = utf8.encode(name);
    result.setRange(0, nameBytes.length > nameSize ? nameSize : nameBytes.length, nameBytes);
    // If randomDelayMax is set, we store it as a special first step: Alt + F15.
    final hasHeader = randomDelayMax > 0;
    final storeLen = (steps.length + (hasHeader ? 1 : 0)).clamp(0, maxSteps);
    result[16] = storeLen;

    var offset = 17;
    if (hasHeader) {
      final header = SequenceStep(mod: 0x04, code: 72, delayMs: randomDelayMax);
      result.setRange(offset, offset + 4, header.toBytes());
      offset += 4;
    }

    for (var i = 0; i < maxSteps - (hasHeader ? 1 : 0); i++) {
      final step = i < steps.length ? steps[i] : SequenceStep.none();
      result.setRange(offset, offset + 4, step.toBytes());
      offset += 4;
    }

    return result;
  }

  Map<String, dynamic> toJson() => {
        'name': name,
        'len': len,
        'random_delay_max': randomDelayMax,
        'steps': steps.map((step) => [step.mod, step.code, step.delayMs]).toList(),
      };

  factory KeySequence.fromJson(Map<String, dynamic> json) {
    final rawSteps = (json['steps'] as List<dynamic>? ?? []).cast<List<dynamic>>();
    final steps = rawSteps.map(SequenceStep.fromJson).toList();
    return KeySequence(
      name: json['name']?.toString() ?? 'Sequence',
      len: (json['len'] as num?)?.toInt() ?? steps.length,
      randomDelayMax: (json['random_delay_max'] as num?)?.toInt() ?? 0,
      steps: steps,
    );
  }

  static List<KeySequence> listFromBytes(Uint8List bytes) {
    if (bytes.isEmpty) return [];

    final perSequenceSize = binarySize;
    if (bytes.length % perSequenceSize != 0) {
      throw ArgumentError('Raw sequences data length must be a multiple of $perSequenceSize bytes');
    }

    final count = bytes.length ~/ perSequenceSize;
    final sequences = <KeySequence>[];
    for (var index = 0; index < count; index++) {
      final offset = index * perSequenceSize;
      final end = offset + perSequenceSize;
      sequences.add(KeySequence.fromBytes(bytes.sublist(offset, end)));
    }
    return sequences;
  }
}
