import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../data/flutter_blue_plus_ble_repository.dart';
import '../domain/ble_connection_state.dart';
import '../domain/ble_device_summary.dart';

final bleRepoProvider = Provider((ref) {
  return FlutterBluePlusBleRepository();
});

final scanProvider = StreamProvider<List<BleDeviceSummary>>((ref) {
  return ref.read(bleRepoProvider).scanResults;
});

final statusProvider = StreamProvider<BleConnectionStatus>((ref) {
  return ref.read(bleRepoProvider).status;
});