import 'package:dashboard/model/pending_advisory.dart';
import 'package:dashboard/model/severity.dart';
import 'package:dashboard/theme.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:flutter/material.dart';

class PendingAdvisoryCard extends StatelessWidget {
  /// The advisory to show.
  final PendingAdvisory advisory;

  /// Whether the advisory is currently being resolved.
  final bool busy;

  /// Called when the operator approves the advisory.
  final VoidCallback onApprove;

  /// Called when the operator rejects the advisory.
  final VoidCallback onReject;

  /// Constructor.
  const PendingAdvisoryCard({
    super.key,
    required this.advisory,
    required this.busy,
    required this.onApprove,
    required this.onReject,
  });

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final accent = _severityColor(theme);
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: accent.withValues(alpha: 0.4)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _header(theme, accent),
            const SizedBox(height: 8),
            _meta(),
            const SizedBox(height: 10),
            Divider(height: 1, color: theme.colorScheme.outlineVariant),
            const SizedBox(height: 10),
            _actionLine(theme),
            const SizedBox(height: 12),
            _actions(theme),
          ],
        ),
      ),
    );
  }

  /// Map severity onto the theme's colours.
  Color _severityColor(ThemeData theme) => switch (advisory.severity) {
    Severity.critical => theme.colorScheme.error,
    Severity.warning || Severity.watch => theme.colorScheme.tertiary,
    Severity.info => theme.colorScheme.secondary,
    Severity.unknown => theme.colorScheme.onSurfaceVariant,
  };

  /// Header kind/source, severity chip.
  Widget _header(ThemeData theme, Color accent) => Row(
    children: [
      Icon(Icons.warning_amber_rounded, size: 18, color: accent),
      const SizedBox(width: 10),
      Expanded(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              advisory.kind,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: theme.colorScheme.onSurface,
              ),
            ),
            Text(
              advisory.sourceId,
              style: TextStyle(
                fontFamily: monoFamily,
                fontSize: 11,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
      StatusChip(label: advisory.severity.label, color: accent),
    ],
  );

  /// Value/unit, disposition, and when it was observed.
  Widget _meta() => Wrap(
    spacing: 16,
    runSpacing: 4,
    children: [
      MetaKeyValue(
        'value',
        advisory.unit.isEmpty
            ? '${advisory.value}'
            : '${advisory.value} ${advisory.unit}',
      ),
      MetaKeyValue('disposition', advisory.disposition),
      MetaKeyValue('observed', _formatTime(advisory.observedAt)),
    ],
  );

  /// What approving this advisory will actually do.
  Widget _actionLine(ThemeData theme) => advisory.hasAction
      ? EntityDetailRow(
          label: 'On approve',
          value: '${advisory.actuatorId} \u2192 ${advisory.targetState}',
        )
      : Row(
          children: [
            Icon(
              Icons.info_outline,
              size: 14,
              color: theme.colorScheme.onSurfaceVariant,
            ),
            const SizedBox(width: 6),
            Expanded(
              child: Text(
                'No rule match to advisory, approval just clears the advisory.',
                style: TextStyle(
                  fontSize: 11,
                  color: theme.colorScheme.onSurfaceVariant,
                ),
              ),
            ),
          ],
        );

  /// Approve/reject buttons plus a busy spinner.
  Widget _actions(ThemeData theme) => Row(
    children: [
      FilledButton.icon(
        onPressed: busy ? null : onApprove,
        icon: const Icon(Icons.check, size: 15),
        label: const Text('Approve'),
        style: FilledButton.styleFrom(
          backgroundColor: theme.colorScheme.secondary,
          foregroundColor: theme.colorScheme.surface,
          disabledBackgroundColor: theme.colorScheme.surfaceContainerHigh,
          visualDensity: VisualDensity.compact,
        ),
      ),
      const SizedBox(width: 8),
      OutlinedButton.icon(
        onPressed: busy ? null : onReject,
        icon: const Icon(Icons.close, size: 15),
        label: const Text('Reject'),
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

  /// Format to HH:MM:SS in local time.
  String _formatTime(DateTime t) {
    final local = t.toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${two(local.hour)}:${two(local.minute)}:${two(local.second)}';
  }
}
