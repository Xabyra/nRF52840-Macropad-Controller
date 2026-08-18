import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:macropad_configurator/features/ble/presentation/connection_screen.dart';
import 'package:macropad_configurator/features/profile/presentation/app_theme.dart';
import 'window_utils.dart' if (dart.library.io) 'window_utils_io.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  configureWindow();
  runApp(const ProviderScope(child: MyApp()));
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Macropad Configurator',
      theme: AppTheme.lightTheme,
      darkTheme: AppTheme.darkTheme,
      themeMode: ThemeMode.dark, // Можна змінити на .light або .system
      home: const ConnectionScreen(),
    );
  }
}