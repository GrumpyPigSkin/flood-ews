import 'package:flutter/material.dart';

/// Helper error banner for showing an error message with dismiss button.
class ErrorBanner extends StatelessWidget {
  /// Error message.
  final String message;

  /// On dismiss callback, optional.
  final VoidCallback? onDismiss;

  /// Optional Icon.
  final IconData? icon;

  /// Optional color.
  final Color? color;

  /// Constructor.
  const ErrorBanner({
    super.key,
    this.icon,
    required this.message,
    this.color,
    this.onDismiss,
  });

  /// Build the banner.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: (color ?? theme.colorScheme.error).withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: (color ?? theme.colorScheme.error).withValues(alpha: 0.4),
        ),
      ),
      child: Row(
        children: [
          Icon(
            icon ?? Icons.error_outline,
            size: 17,
            color: color ?? theme.colorScheme.error,
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              message,
              style: TextStyle(
                color: color ?? theme.colorScheme.error,
                fontSize: 13,
              ),
            ),
          ),
          // Optional dismiss.
          if (onDismiss != null)
            IconButton(
              icon: Icon(
                Icons.close,
                size: 15,
                color: color ?? theme.colorScheme.error,
              ),
              onPressed: onDismiss,
              visualDensity: VisualDensity.compact,
            ),
        ],
      ),
    );
  }
}
