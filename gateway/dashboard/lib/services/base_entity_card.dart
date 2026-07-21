import 'package:flutter/material.dart';

/// Wrapper for a card for a consistent look between screens.
class BaseEntityCard extends StatelessWidget {
  /// Is this card enabled.
  final bool enabled;

  /// Is this card selected.
  final bool selected;

  /// Is this entity busy.
  final bool busy;

  /// Callback when tapped.
  final VoidCallback onTap;

  /// Header widget.
  final Widget header;

  /// Metadata widget.
  final Widget meta;

  /// Details widget.
  final Widget? details;

  /// Callback on edit.
  final VoidCallback onEdit;

  /// Callback when deleted.
  final VoidCallback onDelete;

  /// Constructor.
  const BaseEntityCard({
    super.key,
    required this.enabled,
    required this.selected,
    required this.busy,
    required this.onTap,
    required this.header,
    required this.meta,
    this.details,
    required this.onEdit,
    required this.onDelete,
  });

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Opacity(
      // Dim when disabled.
      opacity: enabled ? 1.0 : 0.55,
      child: Container(
        margin: const EdgeInsets.only(bottom: 10),
        decoration: BoxDecoration(
          color: selected
              ? theme.colorScheme.surfaceContainerHigh
              : theme.colorScheme.surface,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(
            color: selected
                ? theme.colorScheme.primary.withValues(alpha: 0.6)
                : theme.colorScheme.outlineVariant,
          ),
        ),
        child: Material(
          color: Colors.transparent,
          child: InkWell(
            onTap: onTap,
            borderRadius: BorderRadius.circular(10),
            child: Padding(
              padding: const EdgeInsets.all(14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  header,
                  const SizedBox(height: 8),
                  meta,
                  if (selected) ...[
                    const SizedBox(height: 12),
                    Divider(height: 1, color: theme.colorScheme.outlineVariant),
                    const SizedBox(height: 10),
                    ?details,
                    const SizedBox(height: 12),
                    _actions(theme),
                  ],
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  /// Helper for edit and delete buttons.
  Widget _actions(ThemeData theme) => Row(
    children: [
      OutlinedButton.icon(
        onPressed: busy ? null : onEdit,
        icon: const Icon(Icons.edit_outlined, size: 15),
        label: const Text('Edit'),
        style: OutlinedButton.styleFrom(
          foregroundColor: theme.colorScheme.primary,
          side: BorderSide(color: theme.colorScheme.outlineVariant),
          visualDensity: VisualDensity.compact,
        ),
      ),
      const SizedBox(width: 8),
      OutlinedButton.icon(
        onPressed: busy ? null : onDelete,
        icon: const Icon(Icons.delete_outline, size: 15),
        label: const Text('Delete'),
        style: OutlinedButton.styleFrom(
          foregroundColor: theme.colorScheme.error,
          side: BorderSide(
            color: theme.colorScheme.error.withValues(alpha: 0.4),
          ),
          visualDensity: VisualDensity.compact,
        ),
      ),
      const Spacer(),
      if (busy)
        SizedBox(
          width: 15,
          height: 15,
          child: CircularProgressIndicator(
            strokeWidth: 2,
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
    ],
  );
}
