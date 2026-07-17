import 'package:flutter/material.dart';

/// Confirm dialogue is a helper popup for wrapping a message and a confirm/cancel
/// action.
class ConfirmDialogue extends StatelessWidget {
  final String title;
  final String body;
  final String actionLabel;
  final bool danger;

  const ConfirmDialogue({
    super.key,
    required this.title,
    required this.body,
    required this.actionLabel,
    this.danger = false,
  });

  static Future<bool> show(
    BuildContext context, {
    required String title,
    required String body,
    required String actionLabel,
    bool danger = false,
  }) async {
    return await showDialog<bool>(
          context: context,
          builder: (context) => ConfirmDialogue(
            title: title,
            body: body,
            actionLabel: actionLabel,
            danger: danger,
          ),
        ) ??
        false;
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return AlertDialog(
      backgroundColor: theme.colorScheme.surface,
      title: Text(title, style: TextStyle(color: theme.colorScheme.onSurface)),
      content: Text(
        body,
        style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context, false),
          child: Text(
            'Cancel',
            style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
          ),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(context, true),
          style: FilledButton.styleFrom(
            backgroundColor: danger
                ? theme.colorScheme.error
                : theme.colorScheme.primary,
            foregroundColor: theme.colorScheme.surface,
          ),
          child: Text(actionLabel),
        ),
      ],
    );
  }
}
