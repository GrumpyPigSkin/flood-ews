// Policy deep-dive: the rules mapping advisories (from external sources and
// the gateway's own sensor telemetry) onto actuator commands.

import 'package:dashboard/services/base_entity_card.dart';
import 'package:dashboard/widgets/confirmation_dialogue.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/toast.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'package:dashboard/model/policy_rule.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/services/policy_rule_controller.dart';
import 'package:dashboard/widgets/policy_editor_dialogue.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:dashboard/theme.dart';

/// Shows all the policy rules and allows editing.
class PolicyScreen extends StatefulWidget {
  const PolicyScreen({super.key});

  @override
  State<PolicyScreen> createState() => _PolicyScreenState();
}

class _PolicyScreenState extends State<PolicyScreen> {
  PolicyRuleController? _controller;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // Load all the data when the screen is first created, but we don't want
    // to reload all data on every page rebuild.
    _controller ??= PolicyRuleController(context.read<GatewayApi>())..load();
  }

  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  /// Open the RuleEditorDialogue for the selected rule.
  Future<void> _openEditor(
    PolicyRuleController c, {
    PolicyRule? existing,
  }) async {
    final result = await showDialog<PolicyRule>(
      context: context,
      builder: (_) =>
          PolicyEditorDialogue(existing: existing, actuators: c.actuators),
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

  /// Confirmation function for deleting a rule.
  Future<void> _confirmDelete(PolicyRuleController c, PolicyRule r) async {
    final ok = await ConfirmDialogue.show(
      context,
      title: 'Delete ${r.id}?',
      body:
          'Are you sure? Matching advisories will no longer trigger this action.',
      actionLabel: 'Delete',
      danger: true,
    );

    if (ok != true) return;
    try {
      await c.delete(r.id);
      Toast.show(context, 'Deleted ${r.id}');
    } on GatewayApiException catch (e) {
      Toast.show(context, e.message, error: true);
    }
  }

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
      child: Consumer<PolicyRuleController>(
        builder: (context, c, _) {
          if (c.loading && c.rules.isEmpty) {
            return Center(
              child: CircularProgressIndicator(
                color: theme.colorScheme.primary,
              ),
            );
          }

          return ScreenBody(
            intro:
                'Rules mapping advisories onto actuator commands in priority order.',
            children: [
              if (c.error != null)
                ErrorBanner(message: c.error!, onDismiss: c.clearError),
              Panel(
                title: 'Policy rules',
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
                      onPressed: c.actuators.isEmpty
                          ? null
                          : () => _openEditor(c),
                      icon: const Icon(Icons.add, size: 17),
                      label: const Text('Add rule'),
                      style: FilledButton.styleFrom(
                        backgroundColor: theme.colorScheme.primary,
                        foregroundColor: theme.colorScheme.surface,
                        disabledBackgroundColor:
                            theme.colorScheme.surfaceContainerHigh,
                        visualDensity: VisualDensity.compact,
                      ),
                    ),
                  ],
                ),
                child: c.actuators.isEmpty
                    ? const CenteredMessage(
                        icon: Icons.rule_outlined,
                        text: 'Add an actuator before adding policy rules',
                      )
                    : c.rules.isEmpty
                    ? const CenteredMessage(
                        icon: Icons.rule_outlined,
                        text: 'No policy rules configured',
                      )
                    : ListView.builder(
                        shrinkWrap: true,
                        physics: const NeverScrollableScrollPhysics(),
                        itemCount: c.rules.length,
                        itemBuilder: (context, index) {
                          final r = c.rules[index];
                          return _RuleCard(
                            rule: r,
                            actuatorName: c.actuatorName(r.actuatorId),
                            selected: c.selectedId == r.id,
                            busy: c.isBusy(r.id),
                            onTap: () => c.select(r.id),
                            onEdit: () => _openEditor(c, existing: r),
                            onDelete: () => _confirmDelete(c, r),
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

/// One policy rule. Tapping selects it, revealing edit/delete.
class _RuleCard extends StatelessWidget {
  final PolicyRule rule;
  final String actuatorName;
  final bool selected;
  final bool busy;
  final VoidCallback onTap;
  final VoidCallback onEdit;
  final VoidCallback onDelete;

  /// Constructor.
  const _RuleCard({
    required this.rule,
    required this.actuatorName,
    required this.selected,
    required this.busy,
    required this.onTap,
    required this.onEdit,
    required this.onDelete,
  });

  /// Build the widget.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return BaseEntityCard(
      enabled: rule.enabled,
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

  /// Header: name/id, priority badge, require-operator flag if set.
  Widget _header(ThemeData theme) => Row(
    children: [
      Icon(
        rule.enabled ? Icons.rule : Icons.rule_folder_outlined,
        size: 18,
        color: rule.enabled
            ? theme.colorScheme.secondary
            : theme.colorScheme.onSurfaceVariant,
      ),
      const SizedBox(width: 10),
      Expanded(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              rule.name.isEmpty ? rule.id : rule.name,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: theme.colorScheme.onSurface,
              ),
            ),
            Text(
              rule.id,
              style: TextStyle(
                fontFamily: monoFamily,
                fontSize: 11,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
      if (rule.disposition == Disposition.operatorApproved) ...[
        StatusChip(label: 'Needs approval', color: theme.colorScheme.tertiary),
        const SizedBox(width: 6),
      ],
      StatusChip(
        label: rule.matchMinSeverity.label,
        color: theme.colorScheme.primary,
      ),
      const SizedBox(width: 6),
      Icon(
        selected ? Icons.expand_less : Icons.expand_more,
        size: 18,
        color: theme.colorScheme.onSurfaceVariant,
      ),
    ],
  );

  /// Show the match criteria and the resulting action at a glance.
  Widget _meta() => Wrap(
    spacing: 16,
    runSpacing: 4,
    children: [
      MetaKeyValue('kind', rule.matchKind.isEmpty ? 'any' : rule.matchKind),
      MetaKeyValue(
        'source',
        rule.matchSourceId.isEmpty ? 'any' : rule.matchSourceId,
      ),
      MetaKeyValue('priority', rule.priority.toString()),
      MetaKeyValue('action', '$actuatorName = ${rule.targetState}'),
    ],
  );

  /// Details shown when selected.
  Widget _details(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      EntityDetailRow(label: 'Actuator ID', value: rule.actuatorId),
      EntityDetailRow(label: 'Target state', value: rule.targetState),
      EntityDetailRow(label: 'On match', value: rule.disposition.help),
    ],
  );
}
