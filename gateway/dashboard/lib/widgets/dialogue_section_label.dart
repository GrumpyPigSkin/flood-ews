import 'package:flutter/material.dart';

/// Common element between all of the dialogue screens for the section headers.
class DialogueSectionLabel extends StatelessWidget {
  /// The label name.
  final String label;

  /// Constructor.
  const DialogueSectionLabel(this.label, {super.key});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Text(
        label.toUpperCase(),
        style: TextStyle(
          fontSize: 10,
          fontWeight: FontWeight.w700,
          letterSpacing: 1,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
    );
  }
}
