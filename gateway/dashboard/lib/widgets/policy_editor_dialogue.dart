// Add / edit a policy rule.
//
// The actuator + target state are picked from dropdowns backed by the known
// actuator specs, rather than free text, so a rule can't reference an
// actuator id or state that doesn't exist.

import 'package:collection/collection.dart';
import 'package:dashboard/model/actuator.dart';
import 'package:dashboard/model/policy_rule.dart';
import 'package:dashboard/model/severity.dart';
import 'package:dashboard/widgets/console_field_text.dart';
import 'package:dashboard/widgets/dialogue_actions.dart';
import 'package:dashboard/widgets/dialogue_header.dart';
import 'package:dashboard/widgets/dialogue_section_label.dart';
import 'package:dashboard/widgets/severity_picker.dart';
import 'package:flutter/material.dart';

/// The dialogue when a user wants to add or edit a policy rule.
class PolicyEditorDialogue extends StatefulWidget {
  /// Null for a new rule.
  final PolicyRule? existing;

  /// Known actuators, for the actuator/state dropdowns.
  final List<ActuatorSpec> actuators;

  /// Constructor.
  const PolicyEditorDialogue({
    super.key,
    this.existing,
    required this.actuators,
  });

  @override
  State<PolicyEditorDialogue> createState() => _PolicyEditorDialogueState();
}

class _PolicyEditorDialogueState extends State<PolicyEditorDialogue> {
  // Input fields.
  late TextEditingController _id;
  late TextEditingController _name;
  late TextEditingController _matchKind;
  late TextEditingController _matchSourceId;
  late TextEditingController _priority;

  // Non-text fields.
  late Severity _matchMinSeverity;
  String? _actuatorId;
  String? _targetState;
  late bool _requireOperator;
  late bool _enabled;

  bool get _isNew => widget.existing == null;

  /// The chosen actuator's spec, if any (used for the target-state dropdown
  /// and to flag the failsafe state).
  ActuatorSpec? get _actuatorSpec =>
      widget.actuators.where((a) => a.id == _actuatorId).firstOrNull;

  @override
  void initState() {
    super.initState();
    final r = widget.existing ?? PolicyRule.empty();
    _id = TextEditingController(text: r.id);
    _name = TextEditingController(text: r.name);
    _matchKind = TextEditingController(text: r.matchKind);
    _matchSourceId = TextEditingController(text: r.matchSourceId);
    _priority = TextEditingController(text: r.priority.toString());
    _matchMinSeverity = r.matchMinSeverity;
    _requireOperator = r.requireOperator;
    _enabled = r.enabled;

    // Only preselect an actuator/state if it's still one we know about.
    _actuatorId = widget.actuators.any((a) => a.id == r.actuatorId)
        ? r.actuatorId
        : null;
    _targetState =
        _actuatorId != null &&
            (_actuatorSpec?.states.contains(r.targetState) ?? false)
        ? r.targetState
        : null;

    _id.addListener(_updateFormState);
    _priority.addListener(_updateFormState);
  }

  /// Listener for typing.
  void _updateFormState() => setState(() {});

  @override
  void dispose() {
    _id.removeListener(_updateFormState);
    _priority.removeListener(_updateFormState);
    for (final c in [_id, _name, _matchKind, _matchSourceId, _priority]) {
      c.dispose();
    }
    super.dispose();
  }

  /// Ensure the form is valid: needs an id, an actuator, and a target state
  /// that's actually one of that actuator's states.
  bool get _formOk =>
      _id.text.trim().isNotEmpty &&
      _actuatorId != null &&
      _targetState != null &&
      int.tryParse(_priority.text) != null;

  // Submit the new rule.
  void _submit() {
    if (!_formOk) return;

    final rule = PolicyRule(
      id: _id.text.trim(),
      name: _name.text.trim(),
      enabled: _enabled,
      matchKind: _matchKind.text.trim(),
      matchMinSeverity: _matchMinSeverity,
      matchSourceId: _matchSourceId.text.trim(),
      actuatorId: _actuatorId!,
      targetState: _targetState!,
      requireOperator: _requireOperator,
      priority: int.tryParse(_priority.text) ?? 0,
    );
    Navigator.pop(context, rule);
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Dialog(
      backgroundColor: theme.colorScheme.surface,
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 620, maxHeight: 760),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            DialogueHeader(
              isNew: _isNew,
              existingId: widget.existing?.id,
              entityType: "policy rule",
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
                    DialogueSectionLabel('Rule'),
                    ConsoleTextField(
                      controller: _id,
                      label: 'ID',
                      hint: 'e.g. flood-alert-actuate',
                      enabled: _isNew,
                      help: _isNew ? null : 'ID cannot be changed',
                    ),
                    ConsoleTextField(
                      controller: _name,
                      label: 'Name',
                      hint: 'Flood alert -> barrier',
                    ),
                    ConsoleTextField(
                      controller: _priority,
                      label: 'Priority',
                      hint: '0',
                      number: true,
                      help:
                          'Higher wins when multiple rules target the same actuator',
                    ),
                    const SizedBox(height: 16),
                    DialogueSectionLabel('Match criteria (blank = any)'),
                    ConsoleTextField(
                      controller: _matchKind,
                      label: 'Kind',
                      hint: 'e.g. reading, river_level',
                      help: 'Matched against the advisory\'s Kind exactly',
                    ),
                    ConsoleTextField(
                      controller: _matchSourceId,
                      label: 'Source ID',
                      hint: 'e.g. an external source id or node DevEUI',
                    ),
                    SeverityPicker(
                      selectedSeverity: _matchMinSeverity,
                      onSelected: (severity) {
                        setState(() => _matchMinSeverity = severity);
                      },
                      headerText: 'MINIMUM SEVERITY',
                    ),
                    const SizedBox(height: 16),
                    DialogueSectionLabel('Action'),
                    _actuatorPicker(theme),
                    const SizedBox(height: 12),
                    _statePicker(theme),
                    const SizedBox(height: 12),
                    _requireOperatorToggle(theme),
                  ],
                ),
              ),
            ),
            const Divider(height: 1),
            DialogueActions(
              formOk: _formOk,
              errorMessage: 'Pick an actuator and a valid target state',
              submitLabel: _isNew ? 'Add rule' : 'Save changes',
              onSubmit: _submit,
              onCancel: () => Navigator.pop(context),
            ),
          ],
        ),
      ),
    );
  }

  /// Pick the actuator this rule drives. Changing it clears the target
  /// state, since a state valid for one actuator may not exist on another.
  Widget _actuatorPicker(ThemeData theme) => DropdownButtonFormField<String>(
    initialValue: _actuatorId,
    isDense: true,
    dropdownColor: theme.colorScheme.surfaceContainerHigh,
    decoration: InputDecoration(
      labelText: 'Actuator',
      isDense: true,
      filled: true,
      fillColor: theme.colorScheme.surfaceContainerHigh,
      border: OutlineInputBorder(
        borderRadius: BorderRadius.circular(6),
        borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
      ),
    ),
    style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurface),
    hint: Text(
      widget.actuators.isEmpty ? 'No actuators configured' : 'select actuator',
      style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurfaceVariant),
    ),
    items: [
      for (final a in widget.actuators)
        DropdownMenuItem(
          value: a.id,
          child: Text(a.name.isEmpty ? a.id : a.name),
        ),
    ],
    onChanged: (v) => setState(() {
      _actuatorId = v;
      _targetState = null;
    }),
  );

  /// Pick the target state, options are drawn from the selected actuator's
  /// spec, so this is empty/disabled until an actuator is chosen.
  Widget _statePicker(ThemeData theme) {
    final spec = _actuatorSpec;
    return DropdownButtonFormField<String>(
      initialValue: _targetState,
      isDense: true,
      dropdownColor: theme.colorScheme.surfaceContainerHigh,
      decoration: InputDecoration(
        labelText: 'Target state',
        isDense: true,
        filled: true,
        fillColor: theme.colorScheme.surfaceContainerHigh,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(6),
          borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
        ),
      ),
      style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurface),
      hint: Text(
        spec == null ? 'select an actuator first' : 'select state',
        style: TextStyle(
          fontSize: 13,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
      items: spec == null
          ? const []
          : [
              for (final s in spec.states)
                DropdownMenuItem(
                  value: s,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(s),
                      if (s == spec.failsafeState) ...[
                        const SizedBox(width: 6),
                        Icon(
                          Icons.shield_outlined,
                          size: 12,
                          color: theme.colorScheme.tertiary,
                        ),
                      ],
                    ],
                  ),
                ),
            ],
      onChanged: spec == null ? null : (v) => setState(() => _targetState = v),
    );
  }

  /// Toggle whether a match acts autonomously or is queued for an operator.
  Widget _requireOperatorToggle(ThemeData theme) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
    decoration: BoxDecoration(
      color: theme.colorScheme.surfaceContainerHigh,
      borderRadius: BorderRadius.circular(8),
      border: Border.all(color: theme.colorScheme.outlineVariant),
    ),
    child: SwitchListTile(
      dense: true,
      contentPadding: EdgeInsets.zero,
      value: _requireOperator,
      onChanged: (v) => setState(() => _requireOperator = v),
      activeThumbColor: theme.colorScheme.tertiary,
      inactiveThumbColor: theme.colorScheme.onSurfaceVariant,
      inactiveTrackColor: theme.colorScheme.surface,
      title: Text(
        'Require operator approval',
        style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurface),
      ),
      subtitle: Text(
        'A match is queued on the console instead of acting immediately',
        style: TextStyle(
          fontSize: 11,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
    ),
  );
}
