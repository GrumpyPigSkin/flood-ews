import 'package:flutter/material.dart';

/// The title element used in the dialogues.
class DialogueHeader extends StatelessWidget {
  /// Is this a new element or an updated one.
  final bool isNew;

  /// The existing id if editing.
  final String? existingId;

  /// The type of item we are creating if new.
  final String entityType;

  /// Enable flag for the enable button.
  final bool enabled;

  /// Callback if the enable is changed.
  final ValueChanged<bool> onEnabledChanged;

  /// Constructor.
  const DialogueHeader({
    super.key,
    required this.isNew,
    this.existingId,
    required this.entityType,
    required this.enabled,
    required this.onEnabledChanged,
  });

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 18),
      child: Row(
        children: [
          Icon(
            isNew ? Icons.add_circle_outline : Icons.edit_outlined,
            size: 20,
            color: theme.colorScheme.primary,
          ),
          const SizedBox(width: 10),
          Text(
            isNew ? 'Add $entityType' : 'Edit $existingId',
            style: TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w600,
              color: theme.colorScheme.onSurface,
            ),
          ),
          const Spacer(),
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                enabled ? 'ENABLED' : 'DISABLED',
                style: TextStyle(
                  fontSize: 11,
                  color: enabled
                      ? theme.colorScheme.secondary
                      : theme.colorScheme.onSurfaceVariant,
                ),
              ),
              Switch(
                value: enabled,
                onChanged: onEnabledChanged,
                activeThumbColor: theme.colorScheme.secondary,
                inactiveThumbColor: theme.colorScheme.onSurfaceVariant,
                inactiveTrackColor: theme.colorScheme.surfaceContainerHigh,
              ),
            ],
          ),
        ],
      ),
    );
  }
}
