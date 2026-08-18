import 'dart:convert';

class HardwareDescriptor {
  final int rows;
  final int cols;
  final int enc;
  final int led;

  const HardwareDescriptor({
    required this.rows,
    required this.cols,
    required this.enc,
    required this.led,
  });

  factory HardwareDescriptor.fromJson(String jsonString) => HardwareDescriptor.fromMap(json.decode(jsonString));
  factory HardwareDescriptor.fromMap(Map<String, dynamic> map) => HardwareDescriptor(
      rows: map['hw']['rows'], cols: map['hw']['cols'], enc: map['hw']['enc'], led: map['hw']['led'] ?? 0);
}