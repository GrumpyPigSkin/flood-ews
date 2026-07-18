import 'package:flutter/material.dart';

/// Wrapper around a reusable text box.
class ConsoleTextField extends StatelessWidget {
  /// The controller that manages the user input.
  final TextEditingController controller;

  /// The primary title for the text.
  final String label;

  /// Optional example shown inside the input area.
  final String? hint;

  /// Optional help string underneath the input box.
  final String? help;

  /// Numeric input rather than text.
  final bool number;

  /// Whether to mask the characters with dots for a password.
  final bool obscure;

  /// Disable the input.
  final bool enabled;

  /// Constructor.
  const ConsoleTextField({
    super.key,
    required this.controller,
    required this.label,
    this.hint,
    this.help,
    this.number = false,
    this.obscure = false,
    this.enabled = true,
  });

  /// Build the UI for the widget.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          TextField(
            controller: controller,
            enabled: enabled,
            obscureText: obscure,
            keyboardType: number ? TextInputType.number : null,
            style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurface),
            decoration: InputDecoration(
              labelText: label,
              hintText: hint,
              isDense: true,
              labelStyle: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 13,
              ),
              hintStyle: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 12,
              ),
              filled: true,
              fillColor: theme.colorScheme.surfaceContainerHigh,
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(6),
                borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
              ),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(6),
                borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
              ),
            ),
          ),
          if (help != null) ...[
            const SizedBox(height: 4),
            Text(
              help!,
              style: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 10,
              ),
            ),
          ],
        ],
      ),
    );
  }
}
