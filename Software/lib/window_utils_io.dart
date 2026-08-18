import 'dart:io' show Platform;
import 'package:flutter/material.dart';
import 'package:window_size/window_size.dart';

Future<void> configureWindow() async {
  if (Platform.isWindows || Platform.isLinux || Platform.isMacOS) {
    setWindowTitle('Macropad Configurator');
    setWindowMinSize(const Size(900, 620));
    setWindowFrame(const Rect.fromLTWH(0, 0, 1080, 760));
  }
}
