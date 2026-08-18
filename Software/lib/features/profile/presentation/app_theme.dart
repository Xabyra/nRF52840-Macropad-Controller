import 'package:flutter/material.dart';

class AppTheme {
  static const _seedColor = Colors.deepPurple;

  static ThemeData get lightTheme {
    final colorScheme = ColorScheme.fromSeed(
      seedColor: _seedColor,
      brightness: Brightness.light,
    );
    return ThemeData(
      useMaterial3: true,
      colorScheme: colorScheme,
      elevatedButtonTheme: _elevatedButtonTheme(colorScheme),
      inputDecorationTheme: _inputDecorationTheme(colorScheme),
      cardTheme: _cardTheme(),
      chipTheme: _chipTheme(colorScheme),
    );
  }

  static ThemeData get darkTheme {
    final colorScheme = ColorScheme.fromSeed(
      seedColor: _seedColor,
      brightness: Brightness.dark,
    );
    return ThemeData(
      useMaterial3: true,
      colorScheme: colorScheme,
      elevatedButtonTheme: _elevatedButtonTheme(colorScheme),
      inputDecorationTheme: _inputDecorationTheme(colorScheme),
      cardTheme: _cardTheme(),
      chipTheme: _chipTheme(colorScheme),
    );
  }

  static ElevatedButtonThemeData _elevatedButtonTheme(ColorScheme colorScheme) {
    return ElevatedButtonThemeData(
      style: ElevatedButton.styleFrom(
        padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 24),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
        ),
      ),
    );
  }

  static InputDecorationTheme _inputDecorationTheme(ColorScheme colorScheme) {
    return InputDecorationTheme(
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: BorderSide(color: colorScheme.primary, width: 2),
      ),
    );
  }

  static CardThemeData _cardTheme() {
    return CardThemeData(
      elevation: 1,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(16),
      ),
      clipBehavior: Clip.antiAlias,
    );
  }

  static ChipThemeData _chipTheme(ColorScheme colorScheme) {
    return ChipThemeData(
      selectedColor: colorScheme.primaryContainer,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(8),
      ),
      side: BorderSide.none,
    );
  }
}