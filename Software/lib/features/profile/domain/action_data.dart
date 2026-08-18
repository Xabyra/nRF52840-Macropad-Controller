import 'dart:typed_data';

class ActionData {
  final int type; // 0 None, 1 Key, 2 Mouse
  final int mod;  // bitmask
  final int code; // HID code / mouse value

  const ActionData({
    required this.type,
    required this.mod,
    required this.code,
  });

  factory ActionData.none() => const ActionData(type: 0, mod: 0, code: 0);

  Uint8List toBytes() {
    final bd = ByteData(3);
    bd.setUint8(0, type);
    bd.setUint8(1, mod);
    bd.setUint8(2, code);
    return bd.buffer.asUint8List();
  }

  factory ActionData.fromBytes(Uint8List bytes, {int offset = 0}) {
    final bd = ByteData.sublistView(bytes, offset, offset + 3);
    return ActionData(
      type: bd.getUint8(0),
      mod: bd.getUint8(1),
      code: bd.getUint8(2),
    );
  }
}