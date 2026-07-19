import 'package:flutter/material.dart';

/// Actions widget shared between the dialogues.
class DialogueActions extends StatelessWidget {
  /// Is the form ok.
  final bool formOk;

  /// Optional error message string.
  final String? errorMessage;

  /// Submit label.
  final String submitLabel;

  /// Submit callback.
  final VoidCallback? onSubmit;

  /// Cancellation callback.
  final VoidCallback onCancel;

  /// Constructor.
  const DialogueActions({
    super.key,
    required this.formOk,
    this.errorMessage,
    required this.submitLabel,
    required this.onSubmit,
    required this.onCancel,
  });

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          // If the form isn't okay, display the error message if we have it.
          if (!formOk && errorMessage != null)
            Expanded(
              child: Text(
                errorMessage!,
                style: TextStyle(fontSize: 12, color: theme.colorScheme.error),
              ),
            )
          else
            const Spacer(),
          TextButton(
            onPressed: onCancel,
            child: Text(
              'Cancel',
              style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
            ),
          ),
          const SizedBox(width: 8),
          // Only allow submission if the form is ok.
          FilledButton(
            onPressed: formOk ? onSubmit : null,
            style: FilledButton.styleFrom(
              backgroundColor: theme.colorScheme.primary,
              foregroundColor: theme.colorScheme.surface,
              disabledBackgroundColor: theme.colorScheme.surfaceContainerHigh,
            ),
            child: Text(submitLabel),
          ),
        ],
      ),
    );
  }
}
