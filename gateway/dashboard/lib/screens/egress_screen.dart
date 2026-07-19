// Egress deep-dive: the destinations the gateway pushes data OUT to.
//
// Supabase connection feeds the read model that is used on the public gateway.
// Webhooks send data to external authorities for EWS coordination.

import 'package:dashboard/model/egress_target.dart';
import 'package:dashboard/services/base_entity_card.dart';
import 'package:dashboard/services/egress_target_controller.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/widgets/egress_editor_dialogue.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:dashboard/theme.dart';

/// The egress screen for listing egress targets.
class EgressScreen extends StatefulWidget {
  const EgressScreen({super.key});

  @override
  State<EgressScreen> createState() => _EgressScreenState();
}

/// Egress screen state.
class _EgressScreenState extends State<EgressScreen> {
  EgressTargetController? _controller;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _controller ??= EgressTargetController(context.read<GatewayApi>())..load();
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  /// Toast helper to display snackbar.
  void _toast(String message, {bool error = false}) {
    if (!mounted) return;
    final theme = Theme.of(context);
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        backgroundColor: error
            ? theme.colorScheme.error
            : theme.colorScheme.surfaceContainerHigh,
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  /// Open the dialogue to edit or create a new Egress target.
  Future<void> _openEditor(
    EgressTargetController c, {
    EgressTarget? existing,
  }) async {
    final result = await showDialog<EgressEditorResult>(
      context: context,
      builder: (_) => EgressEditorDialogue(existing: existing),
    );
    if (result == null) return;
    try {
      await c.save(result.target, dsn: result.dsn, authToken: result.authToken);
      _toast(
        existing == null
            ? 'Added ${result.target.id}'
            : 'Saved ${result.target.id}',
      );
    } on GatewayApiException catch (e) {
      _toast(e.message, error: true);
    }
  }

  /// Delete a target.
  Future<void> _confirmDelete(EgressTargetController c, EgressTarget t) async {
    final theme = Theme.of(context);
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: theme.colorScheme.surface,
        title: Text(
          'Delete ${t.id}?',
          style: TextStyle(color: theme.colorScheme.onSurface),
        ),
        content: Text(
          t.type == EgressType.supabase
              ? 'The gateway will stop writing telemetry to supabase. '
              : 'The gateway will stop forwarding events to this authority.',
          style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(
              'Cancel',
              style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
            ),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: FilledButton.styleFrom(
              backgroundColor: theme.colorScheme.error,
              foregroundColor: theme.colorScheme.onError,
            ),
            child: const Text('Delete'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    try {
      await c.delete(t.id);
      _toast('Deleted ${t.id}');
    } on GatewayApiException catch (e) {
      _toast(e.message, error: true);
    }
  }

  /// Build the UI.
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
      child: Consumer<EgressTargetController>(
        builder: (context, c, _) {
          if (c.loading && c.targets.isEmpty) {
            return Center(
              child: CircularProgressIndicator(
                color: theme.colorScheme.primary,
              ),
            );
          }

          return ScreenBody(
            intro: 'Destinations the gateway pushes data out to.',
            children: [
              if (c.error != null)
                ErrorBanner(message: c.error!, onDismiss: c.clearError),
              Panel(
                title: 'Egress targets',
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
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
                    FilledButton.icon(
                      onPressed: () => _openEditor(c),
                      icon: const Icon(Icons.add, size: 17),
                      label: const Text('Add target'),
                      style: FilledButton.styleFrom(
                        backgroundColor: theme.colorScheme.primary,
                        foregroundColor: theme.colorScheme.onPrimary,
                        visualDensity: VisualDensity.compact,
                      ),
                    ),
                  ],
                ),
                // List the targets or show the empty screen.
                child: c.targets.isEmpty
                    ? const _EmptyTargets()
                    : Column(
                        children: [
                          for (final t in c.targets)
                            _TargetCard(
                              target: t,
                              selected: c.selectedId == t.id,
                              busy: c.isBusy(t.id),
                              onTap: () => c.select(t.id),
                              onEdit: () => _openEditor(c, existing: t),
                              onDelete: () => _confirmDelete(c, t),
                            ),
                        ],
                      ),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// Target card wraps BaseEntityCard with its custom header, meta, and details.
/// Shows the details for an individual target.
class _TargetCard extends StatelessWidget {
  /// The target.
  final EgressTarget target;

  /// Is the target busy.
  final bool selected;

  /// Is the target selected.
  final bool busy;

  /// Callback when the target is tapped.
  final VoidCallback onTap;

  /// Callback when the edit button is pressed.
  final VoidCallback onEdit;

  /// Callback when the delete button is pressed.
  final VoidCallback onDelete;

  /// Constructor.
  const _TargetCard({
    required this.target,
    required this.selected,
    required this.busy,
    required this.onTap,
    required this.onEdit,
    required this.onDelete,
  });

  /// Build the card.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return BaseEntityCard(
      enabled: target.enabled,
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

  /// Get the icon for the given target type.
  IconData get _typeIcon => switch (target.type) {
    EgressType.supabase => Icons.storage_outlined,
    EgressType.webhook => Icons.webhook_outlined,
    null => Icons.help_outline,
  };

  /// The header for the card. Show the id, name and status chip.
  Widget _header(ThemeData theme) => Row(
    children: [
      Icon(
        _typeIcon,
        size: 18,
        color: target.enabled
            ? theme.colorScheme.secondary
            : theme.colorScheme.onSurfaceVariant,
      ),
      const SizedBox(width: 10),
      Expanded(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              target.name.isEmpty ? target.id : target.name,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: theme.colorScheme.onSurface,
              ),
            ),
            Text(
              target.id,
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
        label: target.type?.label ?? 'unknown',
        color: target.type == null
            ? theme.colorScheme.error
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

  /// Metadata for the target. don't show any secrets, just show that the
  /// secrets are set.
  Widget _meta() => Wrap(
    spacing: 16,
    runSpacing: 4,
    children: [
      MetaKeyValue('forwards', '≥ ${target.minSeverityLevel.label}'),
      if (target.type == EgressType.supabase)
        MetaKeyValue('dsn', target.hasDsn ? 'set' : 'missing')
      else if (target.type == EgressType.webhook)
        MetaKeyValue('auth', target.hasAuthToken ? 'token' : 'none'),
    ],
  );

  /// Details when the card is expanded.
  Widget _details(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      if (target.type == EgressType.webhook)
        EntityDetailRow(
          label: 'URL',
          value: target.webhookUrl.isEmpty ? '(none)' : target.webhookUrl,
        ),
      if (target.type == EgressType.supabase)
        EntityDetailRow(
          label: 'DSN',
          value: target.hasDsn ? '•••••• (stored)' : '(not set)',
        ),
      if (target.type == EgressType.webhook)
        EntityDetailRow(
          label: 'Token',
          value: target.hasAuthToken ? '•••••• (stored)' : '(none)',
        ),
      EntityDetailRow(
        label: 'Min severity',
        value: target.minSeverityLevel.label,
      ),
    ],
  );
}

/// Empty screen for when there are no targets.
class _EmptyTargets extends StatelessWidget {
  const _EmptyTargets();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 32),
      child: Center(
        child: Column(
          children: [
            Icon(
              Icons.cloud_upload_outlined,
              size: 36,
              color: theme.colorScheme.onSurfaceVariant,
            ),
            const SizedBox(height: 10),
            Text(
              'No egress targets configured',
              style: TextStyle(
                color: theme.colorScheme.onSurfaceVariant,
                fontSize: 13,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              'Add a Supabase target to store data, or a '
              'webhook to notify an authority.',
              textAlign: TextAlign.center,
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
