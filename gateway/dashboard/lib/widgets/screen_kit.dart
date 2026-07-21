// Shared building blocks for the deep-dive screens.

import 'package:flutter/material.dart';
import 'package:dashboard/theme.dart';

/// Standard padded, scrollable screen body with an optional intro line.
class ScreenBody extends StatelessWidget {
  final String? intro;
  final List<Widget> children;

  const ScreenBody({super.key, this.intro, required this.children});

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (intro != null) ...[
            Text(
              intro!,
              style: const TextStyle(color: Palette.textDim, fontSize: 14),
            ),
            const SizedBox(height: 16),
          ],
          ...children,
        ],
      ),
    );
  }
}

/// A titled panel/card used to group content on a screen.
class Panel extends StatelessWidget {
  final String title;
  final Widget? trailing;
  final Widget child;

  const Panel({
    super.key,
    required this.title,
    this.trailing,
    required this.child,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 16),
      decoration: BoxDecoration(
        color: Palette.surface,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Palette.hairline),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
            child: Row(
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                    color: Palette.text,
                  ),
                ),
                const Spacer(),
                ?trailing,
              ],
            ),
          ),
          const Divider(height: 1, color: Palette.hairline),
          Padding(padding: const EdgeInsets.all(16), child: child),
        ],
      ),
    );
  }
}

/// Shared key value widget for displaying meta data.
class MetaKeyValue extends StatelessWidget {
  /// String for the key.
  final String keyLabel;

  /// String for the value.
  final String valueLabel;

  /// Constructor.
  const MetaKeyValue(this.keyLabel, this.valueLabel, {super.key});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Text.rich(
      TextSpan(
        children: [
          TextSpan(
            text: '$keyLabel ',
            style: TextStyle(
              fontSize: 11,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
          TextSpan(
            text: valueLabel,
            style: TextStyle(
              fontFamily: monoFamily,
              fontSize: 11,
              color: theme.colorScheme.onSurface,
            ),
          ),
        ],
      ),
    );
  }
}

/// Consistent chip widget.
class StatusChip extends StatelessWidget {
  /// Text inside the chip.
  final String label;

  /// Colour of the chip.
  final Color color;

  /// Constructor.
  const StatusChip({super.key, required this.label, required this.color});

  /// Build the chip UI.
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 10,
          color: color,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}

/// Helper to format a row for the details in the cards for deep-dive screens.
class EntityDetailRow extends StatelessWidget {
  /// The label for the row.
  final String label;

  /// The value for the row.
  final String value;

  /// Optional width for the row for consistent spacing.
  final double labelWidth;

  /// Constructor.
  const EntityDetailRow({
    super.key,
    required this.label,
    required this.value,
    this.labelWidth = 88,
  });

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: labelWidth,
            child: Text(
              label,
              style: TextStyle(
                fontSize: 11,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: TextStyle(
                fontFamily: monoFamily,
                fontSize: 11,
                color: theme.colorScheme.onSurface,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Small helper for when no data is present.
class CenteredMessage extends StatelessWidget {
  // Icon to show.
  final IconData icon;

  // Message.
  final String text;

  /// Constructor.
  const CenteredMessage({super.key, required this.icon, required this.text});

  // Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 48, color: theme.colorScheme.onSurfaceVariant),
          const SizedBox(height: 12),
          Text(
            text,
            style: TextStyle(
              color: theme.colorScheme.onSurfaceVariant,
              fontSize: 15,
            ),
          ),
        ],
      ),
    );
  }
}
