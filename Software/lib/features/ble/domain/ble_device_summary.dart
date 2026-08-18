class BleDeviceSummary {
  final String id;
  final String name;
  final int? rssi;

  const BleDeviceSummary({
    required this.id,
    required this.name,
    this.rssi,
  });
}