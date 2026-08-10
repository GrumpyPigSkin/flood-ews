import 'package:dashboard/model/severity.dart';
import 'package:flutter/material.dart';

/// Severity picker widget used between dialogues.
class SeverityPicker extends StatelessWidget {
  /// Constructor
  const SeverityPicker({
    super.key,
    required this.selectedSeverity,
    required this.onSelected,
    this.headerText,
    this.headerStyle,
    this.padding = const EdgeInsets.only(top: 8, bottom: 4),
    this.chipAlpha = 0.18,
    this.highlightActiveBorder = false,
  });

  final Severity selectedSeverity;
  final ValueChanged<Severity> onSelected;
  final String? headerText;
  final TextStyle? headerStyle;
  final EdgeInsetsGeometry padding;
  final double chipAlpha;
  final bool highlightActiveBorder;

  /// Build the severity picker.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Padding(
      padding: padding,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (headerText != null) ...[
            Text(
              headerText!,
              style:
                  headerStyle ??
                  TextStyle(
                    fontSize: 10,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 1,
                    color: theme.colorScheme.onSurfaceVariant,
                  ),
            ),
            const SizedBox(height: 8),
          ],
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final s in Severity.values)
                () {
                  final isSelected = selectedSeverity == s;
                  return ChoiceChip(
                    label: Text(s.label),
                    selected: isSelected,
                    onSelected: (_) => onSelected(s),
                    backgroundColor: theme.colorScheme.surfaceContainerHigh,
                    selectedColor: theme.colorScheme.primary.withValues(
                      alpha: chipAlpha,
                    ),
                    side: BorderSide(
                      color: isSelected && highlightActiveBorder
                          ? theme.colorScheme.primary
                          : theme.colorScheme.outlineVariant,
                    ),
                    labelStyle: TextStyle(
                      fontSize: 12,
                      color: isSelected
                          ? theme.colorScheme.primary
                          : theme.colorScheme.onSurfaceVariant,
                    ),
                  );
                }(),
            ],
          ),
        ],
      ),
    );
  }
}
