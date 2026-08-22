// External sources deep-dive: the policies governing which outside APIs the
// gateway pulls advisories from.

import 'package:dashboard/services/base_entity_card.dart';
import 'package:dashboard/widgets/confirmation_dialogue.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/toast.dart';
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
      Toast.show(
        context,
        existing == null ? 'Added ${result.id}' : 'Saved ${result.id}',
      );
    } on GatewayApiException catch (e) {
      Toast.show(context, e.message, error: true);
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
      Toast.show(context, 'Deleted ${s.id}');
    } on GatewayApiException catch (e) {
      Toast.show(context, e.message, error: true);
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
                    ? const CenteredMessage(
                        icon: Icons.cloud_off_outlined,
                        text: 'No external sources configured',
                      )
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
    return BaseEntityCard(
      enabled: source.enabled,
      selected: selected,
      busy: busy,
      onTap: onTap,
      header: _header(theme),
      meta: _meta(),
      details: _details(theme),
      onEdit: onEdit,
      onDelete: onDelete,
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
      StatusChip(
        label: source.disposition.label,
        color: source.disposition == Disposition.operatorApproved
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
  Widget _meta() => Wrap(
    spacing: 16,
    runSpacing: 4,
    children: [
      MetaKeyValue('kind', source.kind.isEmpty ? '-' : source.kind),
      MetaKeyValue('every', _dur(source.pollInterval)),
      if (source.maxAgeMs > 0) MetaKeyValue('max age', _dur(source.maxAge)),
      if (source.maxValue != 0)
        MetaKeyValue('bounds', '${_n(source.minValue)}-${_n(source.maxValue)}'),
    ],
  );

  /// Details shown when selected, show the mapping, URL and additional data.
  Widget _details(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      EntityDetailRow(label: 'URL', value: source.url),
      if (source.authHeader.isNotEmpty)
        // Don't display the token.
        EntityDetailRow(label: 'Auth', value: '${source.authHeader}: (hidden)'),
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
