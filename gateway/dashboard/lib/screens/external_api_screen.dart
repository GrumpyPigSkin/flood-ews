// External sources deep-dive: the policies governing which outside APIs the
// gateway pulls advisories from.

import 'package:dashboard/widgets/confirmation_dialogue.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'package:dashboard/model/external_source.dart';
import 'package:dashboard/services/external_source_controller.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:dashboard/widgets/source_editor_dialogue.dart';
import 'package:dashboard/theme.dart';

/// Shows all the external sources and allows editing.
class ExternalApiScreen extends StatefulWidget {
  const ExternalApiScreen({super.key});

  @override
  State<ExternalApiScreen> createState() => _ExternalApiScreenState();
}

/// UI state.
class _ExternalApiScreenState extends State<ExternalApiScreen> {
  ExternalSourceController? _controller;

  @override
  void didChangeDependencies() {
    // Load all the data when the screen is first created, but we don't want to
    // reload all data on every page rebuild.
    super.didChangeDependencies();
    _controller ??= ExternalSourceController(context.read<GatewayApi>())
      ..load();
  }

  /// Dispose of the screen.
  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  /// Wrapper around snackbar.
  void _toast(String message, {bool error = false}) {
    if (!mounted) return;
    final theme = Theme.of(context);
    final messenger = ScaffoldMessenger.of(context);
    messenger.clearSnackBars();
    messenger.showSnackBar(
      SnackBar(
        content: Text(
          message,
          style: TextStyle(
            color: theme.colorScheme.onSurface,
            fontSize: 13,
            fontWeight: FontWeight.w300,
          ),
        ),
        backgroundColor: error
            ? theme.colorScheme.error
            : theme.colorScheme.surfaceContainerHigh,
        behavior: SnackBarBehavior.floating,
        duration: const Duration(seconds: 2),
      ),
    );
  }

  /// Open the SourceEditorDialogue for the selected source.
  Future<void> _openEditor(
    ExternalSourceController c, {
    ExternalSource? existing,
  }) async {
    final result = await showDialog<ExternalSource>(
      context: context,
      builder: (_) => SourceEditorDialogue(existing: existing),
    );
    if (result == null) return;
    try {
      await c.save(result);
      _toast(existing == null ? 'Added ${result.id}' : 'Saved ${result.id}');
    } on GatewayApiException catch (e) {
      _toast(e.message, error: true);
    }
  }

  /// Confirmation function for deleting a source.
  Future<void> _confirmDelete(
    ExternalSourceController c,
    ExternalSource s,
  ) async {
    final ok = await ConfirmDialogue.show(
      context,
      title: 'Delete ${s.id}?',
      body: 'Are you sure? The gateway will stop polling this source.',
      actionLabel: 'Delete',
      danger: true,
    );

    if (ok != true) return;
    try {
      await c.delete(s.id);
      _toast('Deleted ${s.id}');
    } on GatewayApiException catch (e) {
      _toast(e.message, error: true);
    }
  }

  /// Build the main UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final controller = _controller;
    if (controller == null) {
      return Center(
        child: CircularProgressIndicator(color: theme.colorScheme.primary),
      );
    }

    return ChangeNotifierProvider.value(
      value: controller,
      child: Consumer<ExternalSourceController>(
        builder: (context, c, _) {
          if (c.loading && c.sources.isEmpty) {
            return Center(
              child: CircularProgressIndicator(
                color: theme.colorScheme.primary,
              ),
            );
          }

          return ScreenBody(
            intro: 'Sources the gateway polls for advisories.',
            children: [
              if (c.error != null)
                ErrorBanner(message: c.error!, onDismiss: c.clearError),
              Panel(
                title: 'Source policies',
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Refresh button.
                    IconButton(
                      icon: Icon(
                        Icons.refresh,
                        size: 18,
                        color: theme.colorScheme.onSurfaceVariant,
                      ),
                      tooltip: 'Reload',
                      onPressed: c.loading ? null : c.load,
                    ),
                    const SizedBox(width: 4),
                    // Add a new source button.
                    FilledButton.icon(
                      onPressed: () => _openEditor(c),
                      icon: const Icon(Icons.add, size: 17),
                      label: const Text('Add source'),
                      style: FilledButton.styleFrom(
                        backgroundColor: theme.colorScheme.primary,
                        foregroundColor: theme.colorScheme.surface,
                        visualDensity: VisualDensity.compact,
                      ),
                    ),
                  ],
                ),
                // Iterate over the loaded sources.
                child: c.sources.isEmpty
                    ? const _EmptySources()
                    : ListView.builder(
                        shrinkWrap: true,
                        physics: const NeverScrollableScrollPhysics(),
                        itemCount: c.sources.length,
                        itemBuilder: (context, index) {
                          final s = c.sources[index];
                          // Draw the card for the source.
                          return _SourceCard(
                            source: s,
                            selected: c.selectedId == s.id,
                            busy: c.isBusy(s.id),
                            onTap: () => c.select(s.id),
                            onEdit: () => _openEditor(c, existing: s),
                            onDelete: () => _confirmDelete(c, s),
                          );
                        },
                      ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// One source policy. Tapping selects it, revealing edit/delete and mapping.
class _SourceCard extends StatelessWidget {
  final ExternalSource source;
  final bool selected;
  final bool busy;
  final VoidCallback onTap;
  final VoidCallback onEdit;
  final VoidCallback onDelete;

  const _SourceCard({
    required this.source,
    required this.selected,
    required this.busy,
    required this.onTap,
    required this.onEdit,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Opacity(
      // Change the transparency when disabled.
      opacity: source.enabled ? 1.0 : 0.55,
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
                  _header(theme),
                  const SizedBox(height: 8),
                  _meta(theme),
                  // Only show the details and actions when selected.
                  if (selected) ...[
                    const SizedBox(height: 12),
                    Divider(height: 1, color: theme.colorScheme.outlineVariant),
                    const SizedBox(height: 10),
                    _details(theme),
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

  /// Header for an external source, show the name, id, disposition and an
  /// online icon.
  Widget _header(ThemeData theme) => Row(
    children: [
      Icon(
        source.enabled ? Icons.cloud_outlined : Icons.cloud_off_outlined,
        size: 18,
        color: source.enabled
            ? theme.colorScheme.secondary
            : theme.colorScheme.onSurfaceVariant,
      ),
      const SizedBox(width: 10),
      Expanded(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              source.name.isEmpty ? source.id : source.name,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: theme.colorScheme.onSurface,
              ),
            ),
            Text(
              source.id,
              style: TextStyle(
                fontFamily: monoFamily,
                fontSize: 11,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
      _chip(
        source.disposition.label,
        source.disposition == Disposition.operatorApproved
            ? theme.colorScheme.tertiary
            : theme.colorScheme.primary,
      ),
      const SizedBox(width: 6),
      Icon(
        selected ? Icons.expand_less : Icons.expand_more,
        size: 18,
        color: theme.colorScheme.onSurfaceVariant,
      ),
    ],
  );

  /// Show the kind, poll interval, max age and bounds if present.
  Widget _meta(ThemeData theme) => Wrap(
    spacing: 16,
    runSpacing: 4,
    children: [
      _kv(theme, 'kind', source.kind.isEmpty ? '-' : source.kind),
      _kv(theme, 'every', _dur(source.pollInterval)),
      if (source.maxAgeMs > 0) _kv(theme, 'max age', _dur(source.maxAge)),
      if (source.maxValue != 0)
        _kv(theme, 'bounds', '${_n(source.minValue)}-${_n(source.maxValue)}'),
    ],
  );

  /// Details shown when selected, show the mapping, URL and additional data.
  Widget _details(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      _detailRow(theme, 'URL', source.url),
      if (source.authHeader.isNotEmpty)
        // Don't display the token.
        _detailRow(theme, 'Auth', '${source.authHeader}: (hidden)'),
      const SizedBox(height: 8),
      Text(
        'FIELD MAPPING',
        style: TextStyle(
          fontSize: 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 1,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
      const SizedBox(height: 4),
      Container(
        width: double.infinity,
        padding: const EdgeInsets.all(10),
        decoration: BoxDecoration(
          color: theme.colorScheme.surface,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: theme.colorScheme.outlineVariant),
        ),
        child: Text(
          source.fieldMap.isEmpty ? '(none)' : source.fieldMap,
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 11,
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
      ),
    ],
  );

  /// Helper for formatting the detail row.
  Widget _detailRow(ThemeData theme, String k, String v) => Padding(
    padding: const EdgeInsets.only(bottom: 4),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: 44,
          child: Text(
            k,
            style: TextStyle(
              fontSize: 11,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
        ),
        Expanded(
          child: Text(
            v,
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

  /// Action buttons to edit and delete the source.
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

  /// Format a key value for the meta field, like "kind": "rainfall"
  static Widget _kv(ThemeData theme, String k, String v) => Text.rich(
    TextSpan(
      children: [
        TextSpan(
          text: '$k ',
          style: TextStyle(
            fontSize: 11,
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
        TextSpan(
          text: v,
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 11,
            color: theme.colorScheme.onSurface,
          ),
        ),
      ],
    ),
  );

  /// The small chip in the top RHS showing disposition.
  static Widget _chip(String label, Color color) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
    decoration: BoxDecoration(
      color: color.withValues(alpha: 0.14),
      borderRadius: BorderRadius.circular(4),
    ),
    child: Text(
      label,
      style: TextStyle(fontSize: 10, color: color, fontWeight: FontWeight.w600),
    ),
  );

  /// Helper to format the duration.
  static String _dur(Duration d) {
    if (d.inHours >= 1) return '${d.inHours}h';
    if (d.inMinutes >= 1) return '${d.inMinutes}m';
    return '${d.inSeconds}s';
  }

  /// Helper to display the min/max value.
  static String _n(double d) =>
      d == d.roundToDouble() ? d.toInt().toString() : d.toString();
}

/// Empty sources is shown where there are no sources current setup.
class _EmptySources extends StatelessWidget {
  const _EmptySources();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 32),
      child: Center(
        child: Column(
          children: [
            Icon(
              Icons.cloud_off_outlined,
              size: 36,
              color: theme.colorScheme.onSurfaceVariant,
            ),
            const SizedBox(height: 10),
            Text(
              'No external sources configured',
              style: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 13,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              'Add one to start pulling advisories from a partner API.',
              style: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 11,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
