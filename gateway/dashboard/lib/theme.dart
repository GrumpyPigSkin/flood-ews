// Theme for EWS dashboard. The theme is all dark mode with colours to highlight
// specific important data to the user. The aim is to keep the noise low in the
// screens for a easier diagnosis if issues. And give more important data it's
// visual affordance.

import 'package:flutter/material.dart';

// Colours used throughout the app.
class Palette {
  static const ground = Color(0xFF0F172A);
  static const surface = Color(0xFF1E293B);
  static const surfaceAlt = Color(0xFF273449);
  static const hairline = Color(0xFF334155);
  static const text = Color(0xFFE2E8F0);
  static const textDim = Color(0xFF94A3B8);

  static const ok = Color(0xFF34D399);
  static const watch = Color(0xFFFBBF24);
  static const alarm = Color(0xFFF87171);
  static const accent = Color(0xFF38BDF8);
}

/// Build the main theme for the app.
ThemeData buildTheme() {
  final base = ThemeData.dark(useMaterial3: true);
  return base.copyWith(
    scaffoldBackgroundColor: Palette.ground,
    colorScheme: base.colorScheme.copyWith(
      surface: Palette.surface,
      surfaceContainerHigh: Palette.surfaceAlt,
      primary: Palette.accent,
      error: Palette.alarm,
      tertiary: Palette.watch,
      secondary: Palette.ok,
      outlineVariant: Palette.hairline,
      onSurface: Palette.text,
      onSurfaceVariant: Palette.textDim,
    ),
    textTheme: base.textTheme.apply(
      bodyColor: Palette.text,
      displayColor: Palette.text,
    ),
    dividerTheme: const DividerThemeData(color: Palette.hairline, thickness: 1),
    appBarTheme: const AppBarTheme(
      backgroundColor: Palette.ground,
      elevation: 0,
      centerTitle: false,
    ),
  );
}

/// Monospace family for numeric/hex data.
const String monoFamily = 'monospace';
