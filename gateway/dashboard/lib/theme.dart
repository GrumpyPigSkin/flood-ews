// High contrast theme for EWS dashboard. The idea is make the whole application
// default high contrast for greater readability and WCAG 1.4.6 Contrast

import 'package:flutter/material.dart';

class Palette {
  // Background colours, dark for high contrast text.
  static const ground = Color(0xFF000000);
  static const surface = Color(0xFF121212);
  static const surfaceAlt = Color(0xFF1E1E1E);

  // Outline colours
  static const hairline = Color(0xFF444444);
  static const borderActive = Color(0xFFFFFFFF);

  // Bright text for high contrast
  static const text = Color(0xFFFFFFFF);
  static const textDim = Color(0xFFB0B0B0);

  // OK colour for outline and container.
  static const ok = Color(0xFF00FF66);
  static const okContainer = Color(0xFF003311);

  // Warning colour for outline and container.
  static const watch = Color(0xFFFFCC00);
  static const watchContainer = Color(0xFF332600);

  // Alarm colour for outline and container.
  static const alarm = Color(0xFFFF3333);
  static const alarmContainer = Color(0xFF330000);

  // Primary colour for none status stuff
  static const systemPrimary = Color(0xFF00E5FF);
}

/// Build the main theme for the app.
ThemeData buildTheme() {
  final base = ThemeData.dark(useMaterial3: true);
  return base.copyWith(
    scaffoldBackgroundColor: Palette.ground,
    colorScheme: base.colorScheme.copyWith(
      surface: Palette.surface,
      surfaceContainerHigh: Palette.surfaceAlt,
      primary: Palette.systemPrimary,
      error: Palette.alarm,
      errorContainer: Palette.alarmContainer,
      tertiary: Palette.watch,
      tertiaryContainer: Palette.watchContainer,
      secondary: Palette.ok,
      secondaryContainer: Palette.okContainer,
      outline: Palette.borderActive,
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
