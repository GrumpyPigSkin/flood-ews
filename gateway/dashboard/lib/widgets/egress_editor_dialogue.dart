// Add/edit an egress target.

import 'package:dashboard/model/egress_target.dart';
import 'package:dashboard/widgets/console_field_text.dart';
import 'package:dashboard/widgets/dialogue_actions.dart';
import 'package:dashboard/widgets/dialogue_header.dart';
import 'package:dashboard/widgets/dialogue_section_label.dart';
import 'package:flutter/material.dart';

/// Result of the editor. The target plus optional secrets, DSN and Auth Token.
class EgressEditorResult {
  final EgressTarget target;
  final String? dsn;
  final String? authToken;
  const EgressEditorResult(this.target, {this.dsn, this.authToken});
}

/// The dialogue UI.
class EgressEditorDialogue extends StatefulWidget {
  final EgressTarget? existing;
  const EgressEditorDialogue({super.key, this.existing});

  @override
  State<EgressEditorDialogue> createState() => _EgressEditorDialogueState();
}

/// The UI state.
class _EgressEditorDialogueState extends State<EgressEditorDialogue> {
  /// Input fields.
  late TextEditingController _id;
  late TextEditingController _name;
  late TextEditingController _webhookUrl;
  late TextEditingController _dsn;
  late TextEditingController _authToken;

  /// The selected type for the egress type.
  late EgressType _type;

  /// The min severity to send at.
  late int _minSeverity;

  /// Is this target enabled.
  late bool _enabled;

  /// True if no existing target was passed.
  bool get _isNew => widget.existing == null;

  /// Init state, initialise the fields with the target passed in or defaults.
  @override
  void initState() {
    super.initState();
    final t = widget.existing ?? EgressTarget.empty();
    _id = TextEditingController(text: t.id)..addListener(_updateFormState);
    _name = TextEditingController(text: t.name);
    _webhookUrl = TextEditingController(text: t.webhookUrl)
      ..addListener(_updateFormState);
    _dsn = TextEditingController()..addListener(_updateFormState);
    _authToken = TextEditingController();
    _type = t.type ?? EgressType.webhook;
    _minSeverity = t.minSeverity;
    _enabled = t.enabled;
  }

  /// Update state callback.
  void _updateFormState() {
    setState(() {});
  }

  /// Dispose of the dialogue.
  @override
  void dispose() {
    _id.removeListener(_updateFormState);
    _webhookUrl.removeListener(_updateFormState);
    _dsn.removeListener(_updateFormState);
    for (final c in [_id, _name, _webhookUrl, _dsn, _authToken]) {
      c.dispose();
    }
    super.dispose();
  }

  /// Whether the DSN has been entered, even on edit the DSN needs to be
  /// re-entered.
  bool get _dsnSatisfied => _dsn.text.trim().isNotEmpty;

  /// Whether the URL has been entered, even on edit the URL needs to be
  /// re-entered.
  bool get _urlSatisfied => _webhookUrl.text.trim().isNotEmpty;

  /// Whether form is okay depends on the type of form and the required data is
  /// entered.
  bool get _formOk {
    if (_id.text.trim().isEmpty) return false;
    return switch (_type) {
      EgressType.supabase => _dsnSatisfied,
      EgressType.webhook => _urlSatisfied,
    };
  }

  /// Submit the EgressTarget.
  void _submit() {
    if (!_formOk) return;
    final target = EgressTarget(
      id: _id.text.trim(),
      name: _name.text.trim(),
      enabled: _enabled,
      type: _type,
      minSeverity: _minSeverity,
      webhookUrl: _type == EgressType.webhook ? _webhookUrl.text.trim() : '',
      hasDsn: _type == EgressType.supabase && _dsnSatisfied,
      hasWebhookUrl: _type == EgressType.webhook && _urlSatisfied,
      hasAuthToken: _authToken.text.trim().isNotEmpty,
    );

    /// Close the dialogue.
    Navigator.pop(
      context,
      EgressEditorResult(
        target,
        dsn: _type == EgressType.supabase ? _dsn.text.trim() : null,
        authToken:
            _type == EgressType.webhook && _authToken.text.trim().isNotEmpty
            ? _authToken.text.trim()
            : null,
      ),
    );
  }

  /// Draw the dialogue UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Dialog(
      backgroundColor: theme.colorScheme.surface,
      child: ConstrainedBox(
        constraints: BoxConstraints(
          maxWidth: 560,
          maxHeight: MediaQuery.sizeOf(context).height * 0.85,
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            DialogueHeader(
              isNew: _isNew,
              existingId: widget.existing?.id,
              entityType: "egress target",
              enabled: _enabled,
              onEnabledChanged: (v) => setState(() => _enabled = v),
            ),
            const Divider(height: 1),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(20),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    DialogueSectionLabel('Target'),
                    ConsoleTextField(
                      controller: _id,
                      label: 'ID',
                      hint: 'cloud-readmodel',
                      enabled: _isNew,
                      help: _isNew ? null : 'ID cannot be changed',
                    ),
                    ConsoleTextField(
                      controller: _name,
                      label: 'Name',
                      hint: 'Cloud read-model',
                    ),
                    const SizedBox(height: 16),
                    // Type selection.
                    DialogueSectionLabel('Type'),
                    _typePicker(theme),
                    const SizedBox(height: 16),
                    // Show the type specific fields.
                    _typeSpecificFields(theme),
                    const SizedBox(height: 16),
                    DialogueSectionLabel('Forwarding threshold'),
                    _severityPicker(theme),
                  ],
                ),
              ),
            ),
            Divider(height: 1, color: theme.colorScheme.outlineVariant),
            // Actions at the bottom.
            DialogueActions(
              formOk: _formOk,
              errorMessage: _id.text.trim().isEmpty
                  ? 'An ID is required'
                  : _type == EgressType.supabase
                  ? 'Enter the DSN to save'
                  : 'Enter the webhook URL to save',
              submitLabel: _isNew ? 'Add target' : 'Save changes',
              onSubmit: _submit,
              onCancel: () => Navigator.pop(context),
            ),
          ],
        ),
      ),
    );
  }

  /// Pick either webhook or supabase.
  Widget _typePicker(ThemeData theme) => Row(
    children: [
      for (final t in EgressType.values)
        Expanded(
          child: Padding(
            padding: EdgeInsets.only(
              right: t == EgressType.values.first ? 10 : 0,
            ),
            child: _TypeTile(
              type: t,
              selected: _type == t,
              onTap: () => setState(() => _type = t),
            ),
          ),
        ),
    ],
  );

  /// Show the specific field depending on the type selected.
  Widget _typeSpecificFields(ThemeData theme) {
    switch (_type) {
      // For supabase all that is needed is the DSN.
      case EgressType.supabase:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            DialogueSectionLabel('Connection'),
            ConsoleTextField(
              controller: _dsn,
              label: 'Database connection string (DSN)',
              hint: 'postgresql://...',
              obscure: true,
            ),
            _secretNote(
              theme,
              set: widget.existing?.hasDsn ?? false,
              label: 'DSN',
            ),
          ],
        );
      // For webhook we need a URL and a possible token.
      case EgressType.webhook:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            DialogueSectionLabel('Endpoint'),
            ConsoleTextField(
              controller: _webhookUrl,
              label: 'Webhook URL',
              hint: 'https://authority.example.org/ingest',
            ),
            const SizedBox(height: 4),
            ConsoleTextField(
              controller: _authToken,
              label: 'Bearer token (optional)',
              hint: '••••••',
              obscure: true,
            ),
            _secretNote(
              theme,
              set: widget.existing?.hasAuthToken ?? false,
              label: 'token',
            ),
          ],
        );
    }
  }

  /// Make sure the user knows they need to re-provide the url/token or DSN when
  /// they edit the target. Made yellow so it stands out on the screen.
  Widget _secretNote(
    ThemeData theme, {
    required bool set,
    required String label,
  }) {
    if (_isNew) return const SizedBox.shrink();
    return Padding(
      padding: const EdgeInsets.only(top: 2, bottom: 4),
      child: Row(
        children: [
          Icon(
            set ? Icons.info_outline : Icons.remove_circle_outline,
            size: 13,
            color: set
                ? theme.colorScheme.tertiary
                : theme.colorScheme.onSurfaceVariant,
          ),
          const SizedBox(width: 6),
          Expanded(
            child: Text(
              set
                  ? 'A $label is already stored, but it cannot be shown. '
                        'Re-enter it to keep this target working, saving '
                        'without it will clear the stored value.'
                  : 'No $label currently stored.',
              style: TextStyle(
                fontSize: 11,
                color: set
                    ? theme.colorScheme.tertiary
                    : theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _severityPicker(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      Text(
        _type == EgressType.webhook
            ? 'Only events at or above this severity are forwarded.'
            : 'Only events at or above this severity are written to the '
                  'read-model.',
        style: TextStyle(
          fontSize: 12,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
      const SizedBox(height: 10),
      Wrap(
        spacing: 8,
        children: [
          for (final s in Severity.values)
            ChoiceChip(
              label: Text(s.label),
              selected: _minSeverity == s.level,
              onSelected: (_) => setState(() => _minSeverity = s.level),
              backgroundColor: theme.colorScheme.surfaceContainerHigh,
              selectedColor: theme.colorScheme.primary.withValues(alpha: 0.25),
              side: BorderSide(
                color: _minSeverity == s.level
                    ? theme.colorScheme.primary
                    : theme.colorScheme.outlineVariant,
              ),
              labelStyle: TextStyle(
                fontSize: 12,
                color: _minSeverity == s.level
                    ? theme.colorScheme.primary
                    : theme.colorScheme.onSurfaceVariant,
              ),
            ),
        ],
      ),
    ],
  );
}

/// Helper widget for showing data for either Supabase or Webhook.
class _TypeTile extends StatelessWidget {
  /// The egress type.
  final EgressType type;

  /// Is this Type already selected.
  final bool selected;

  /// Callback when selected.
  final VoidCallback onTap;

  /// Constructor
  const _TypeTile({
    required this.type,
    required this.selected,
    required this.onTap,
  });

  /// Build the widget.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(8),
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: selected
              ? theme.colorScheme.primary.withValues(alpha: 0.14)
              : theme.colorScheme.surfaceContainerHigh,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: selected
                ? theme.colorScheme.primary
                : theme.colorScheme.outlineVariant,
          ),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                /// Show an icon dependent on the type.
                Icon(
                  type == EgressType.supabase
                      ? Icons.storage_outlined
                      : Icons.webhook_outlined,
                  size: 16,
                  color: selected
                      ? theme.colorScheme.primary
                      : theme.colorScheme.onSurfaceVariant,
                ),
                const SizedBox(width: 8),
                // The label for the type.
                Text(
                  type.label,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: selected
                        ? theme.colorScheme.primary
                        : theme.colorScheme.onSurface,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 4),
            // Show the help for the type.
            Text(
              type.help,
              style: TextStyle(
                fontSize: 11,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
