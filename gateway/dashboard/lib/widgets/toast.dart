import 'package:flutter/material.dart';

// Shared snackbar toast.
class Toast {
  Toast._();

  /// Show `message` in a snackbar.
  static void show(BuildContext context, String message, {bool error = false}) {
    final theme = Theme.of(context);
    final messenger = ScaffoldMessenger.of(context);
    messenger.clearSnackBars();
    messenger.showSnackBar(
      SnackBar(
        content: Text(
          message,
          style: TextStyle(
            color: theme.colorScheme.onSurface,
            fontSize: 13,
            fontWeight: FontWeight.w300,
          ),
        ),
        backgroundColor: error
            ? theme.colorScheme.error
            : theme.colorScheme.surfaceContainerHigh,
        behavior: SnackBarBehavior.floating,
        duration: const Duration(seconds: 2),
      ),
    );
  }
}
