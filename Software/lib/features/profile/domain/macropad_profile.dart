import 'dart:typed_data';
import 'dart:convert';
import 'action_data.dart';

class MacropadProfile {
  static const int binarySize = 61;
  static const int nameSize = 16;
  static const int maxKeys = 12; // 3x4
  static const int actionSize = 3;

  final String name;
  final int rows;
  final int cols;
  final List<ActionData> matrix; // row-major, max 12
  final ActionData encBtn;
  final ActionData encCw;
  final ActionData encCcw;

  const MacropadProfile({
    required this.name,
    required this.rows,
    required this.cols,
    required this.matrix,
    required this.encBtn,
    required this.encCw,
    required this.encCcw,
  });

  MacropadProfile copyWith({
    String? name,
    int? rows,
    int? cols,
    List<ActionData>? matrix,
    ActionData? encBtn,
    ActionData? encCw,
    ActionData? encCcw,
  }) {
    return MacropadProfile(
      name: name ?? this.name,
      rows: rows ?? this.rows,
      cols: cols ?? this.cols,
      matrix: matrix ?? this.matrix,
      encBtn: encBtn ?? this.encBtn,
      encCw: encCw ?? this.encCw,
      encCcw: encCcw ?? this.encCcw,
    );
  }

  factory MacropadProfile.empty({
    required int rows,
    required int cols,
  }) {
    return MacropadProfile(
      name: 'My Macropad',
      rows: rows,
      cols: cols,
      matrix: List.generate(rows * cols, (_) => ActionData.none()),
      encBtn: ActionData.none(),
      encCw: ActionData.none(),
      encCcw: ActionData.none(),
    );
  }

  Uint8List toBytes() {
    if (rows * cols > maxKeys) {
      throw ArgumentError('rows*cols must be <= $maxKeys');
    }

    final bytes = Uint8List(binarySize);

    // name: 16 bytes, null-padded
    final nameBytes = utf8.encode(name);
    bytes.setRange(0, nameBytes.length > nameSize ? nameSize : nameBytes.length, nameBytes);

    var offset = nameSize;

    // matrix: always reserve 12 actions (36 bytes)
    for (var i = 0; i < maxKeys; i++) {
      final action = i < matrix.length ? matrix[i] : ActionData.none();
      bytes.setRange(offset, offset + actionSize, action.toBytes());
      offset += actionSize;
    }

    // encoder actions: 3 * 3 bytes = 9 bytes
    for (final action in [encBtn, encCw, encCcw]) {
      bytes.setRange(offset, offset + actionSize, action.toBytes());
      offset += actionSize;
    }

    return bytes;
  }

  factory MacropadProfile.fromBytes(
    Uint8List bytes, {
    required int rows,
    required int cols,
  }) {
    if (bytes.length != binarySize) {
      throw ArgumentError('Profile must be exactly $binarySize bytes');
    }

    final nameBytes = bytes.sublist(0, nameSize);
    final name = utf8.decode(nameBytes.where((b) => b != 0).toList(), allowMalformed: true);

    var offset = nameSize;
    final matrix = <ActionData>[];

    for (var i = 0; i < maxKeys; i++) {
      matrix.add(ActionData.fromBytes(bytes, offset: offset));
      offset += actionSize;
    }

    final encBtn = ActionData.fromBytes(bytes, offset: offset);
    offset += actionSize;

    final encCw = ActionData.fromBytes(bytes, offset: offset);
    offset += actionSize;

    final encCcw = ActionData.fromBytes(bytes, offset: offset);

    return MacropadProfile(
      name: name,
      rows: rows,
      cols: cols,
      matrix: matrix.take(rows * cols).toList(),
      encBtn: encBtn,
      encCw: encCw,
      encCcw: encCcw,
    );
  }
}