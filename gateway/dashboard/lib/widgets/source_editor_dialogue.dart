// Add / edit an external source.
//
// The field mapping is validated, if there is a fatal issue with the field
// mapping then the user is blocked from adding it until the issues are fixed.

import 'package:dashboard/widgets/console_field_text.dart';
import 'package:dashboard/widgets/dialogue_actions.dart';
import 'package:dashboard/widgets/dialogue_header.dart';
import 'package:dashboard/widgets/dialogue_section_label.dart';
import 'package:dashboard/widgets/status_issue_list.dart';
import 'package:flutter/material.dart';

import 'package:dashboard/model/external_source.dart';
import 'package:dashboard/theme.dart';

/// The dialogue when a user wants to enter a new editor
class SourceEditorDialogue extends StatefulWidget {
  /// Null for a new source.
  final ExternalSource? existing;

  const SourceEditorDialogue({super.key, this.existing});

  @override
  State<SourceEditorDialogue> createState() => _SourceEditorDialogueState();
}

/// The state for the editor dialogue
class _SourceEditorDialogueState extends State<SourceEditorDialogue> {
  // Text fields.
  late TextEditingController _id;
  late TextEditingController _name;
  late TextEditingController _url;
  late TextEditingController _kind;
  late TextEditingController _authHeader;
  late TextEditingController _authToken;
  late TextEditingController _pollSecs;
  late TextEditingController _maxAgeSecs;
  late TextEditingController _minValue;
  late TextEditingController _maxValue;
  late TextEditingController _fieldMap;

  // Disposition of the source.
  late bool _enabled;

  // Any issues with fields in the source.
  List<FieldMapIssue> _issues = const [];

  bool get _isNew => widget.existing == null;

  // Initialise the dialogue.
  @override
  void initState() {
    super.initState();
    final s = widget.existing ?? ExternalSource.empty();
    _id = TextEditingController(text: s.id);
    _name = TextEditingController(text: s.name);
    _url = TextEditingController(text: s.url);
    _kind = TextEditingController(text: s.kind);
    _authHeader = TextEditingController(text: s.authHeader);
    _authToken = TextEditingController(text: s.authToken);
    _pollSecs = TextEditingController(text: (s.pollMs ~/ 1000).toString());
    _maxAgeSecs = TextEditingController(text: (s.maxAgeMs ~/ 1000).toString());
    _minValue = TextEditingController(text: _num(s.minValue));
    _maxValue = TextEditingController(text: _num(s.maxValue));
    _fieldMap = TextEditingController(
      text: s.fieldMap.isEmpty && _isNew
          ? FieldMapValidator.template
          : s.fieldMap,
    );
    _enabled = s.enabled;

    _id.addListener(_updateFormState);
    _url.addListener(_updateFormState);
    _pollSecs.addListener(_updateFormState);

    _fieldMap.addListener(_revalidate);
    _revalidate();
  }

  static String _num(double d) =>
      d == d.roundToDouble() ? d.toInt().toString() : d.toString();

  /// Helper to validate the field map.
  void _revalidate() {
    setState(() => _issues = FieldMapValidator.validate(_fieldMap.text));
  }

  /// Listener for typing.
  void _updateFormState() {
    setState(() {});
  }

  /// Dispose of the dialogue.
  @override
  void dispose() {
    _id.removeListener(_updateFormState);
    _url.removeListener(_updateFormState);
    _pollSecs.removeListener(_updateFormState);
    _fieldMap.removeListener(_revalidate);

    for (final c in [
      _id,
      _name,
      _url,
      _kind,
      _authHeader,
      _authToken,
      _pollSecs,
      _maxAgeSecs,
      _minValue,
      _maxValue,
      _fieldMap,
    ]) {
      c.dispose();
    }
    super.dispose();
  }

  /// Check for any fatal errors in the field mapping.
  bool get _fieldMapOk => !_issues.any((i) => i.fatal);

  /// Ensure the form is valid.
  bool get _formOk =>
      _id.text.trim().isNotEmpty &&
      _url.text.trim().isNotEmpty &&
      (int.tryParse(_pollSecs.text) ?? 0) >= 1 &&
      _fieldMapOk;

  // Submit the new source.
  void _submit() {
    if (!_formOk) return;

    final pollSeconds = int.tryParse(_pollSecs.text) ?? 60;

    final source = ExternalSource(
      id: _id.text.trim(),
      name: _name.text.trim(),
      enabled: _enabled,
      url: _url.text.trim(),
      authHeader: _authHeader.text.trim(),
      authToken: _authToken.text.trim(),
      pollMs: pollSeconds * 1000,
      kind: _kind.text.trim(),
      maxAgeMs: (int.tryParse(_maxAgeSecs.text) ?? 900) * 1000,
      minValue: double.tryParse(_minValue.text) ?? 0,
      maxValue: double.tryParse(_maxValue.text) ?? 0,
      fieldMap: _fieldMap.text.trim(),
    );
    Navigator.pop(context, source);
  }

  /// The main UI for the form.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Dialog(
      backgroundColor: theme.colorScheme.surface,
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 860, maxHeight: 720),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            DialogueHeader(
              isNew: _isNew,
              existingId: widget.existing?.id,
              entityType: "external source",
              enabled: _enabled,
              onEnabledChanged: (v) => setState(() => _enabled = v),
            ),
            const Divider(height: 1),
            Expanded(
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(20),
                child: LayoutBuilder(
                  builder: (context, c) {
                    // Ensure there is enough screen real estate to fit the two
                    // columns, or fallback to a big row.
                    final twoCol = c.maxWidth > 640;
                    final left = _detailsColumn(theme);
                    final right = _mappingColumn(theme);
                    if (!twoCol) {
                      return Column(
                        children: [left, const SizedBox(height: 20), right],
                      );
                    }
                    return Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Expanded(child: left),
                        const SizedBox(width: 24),
                        Expanded(child: right),
                      ],
                    );
                  },
                ),
              ),
            ),
            const Divider(height: 1),
            DialogueActions(
              formOk: _fieldMapOk,
              errorMessage: 'Fix the field mapping before saving',
              submitLabel: _isNew ? 'Add source' : 'Save changes',
              onSubmit: _submit,
              onCancel: () => Navigator.pop(context),
            ),
          ],
        ),
      ),
    );
  }

  /// Details column contains:
  /// - ID
  /// - Name
  /// - URL
  /// - Kind
  /// - Authentication
  /// - Polling interval and Max age
  /// - Bounds checking min/max
  Widget _detailsColumn(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      DialogueSectionLabel('Source'),
      ConsoleTextField(
        controller: _id,
        label: 'ID',
        hint: 'e.g. environment-agency',
        enabled: _isNew,
        help: _isNew ? null : 'ID cannot be changed',
      ),
      ConsoleTextField(
        controller: _name,
        label: 'Name',
        hint: 'Environment Agency river gauge',
      ),
      ConsoleTextField(
        controller: _url,
        label: 'URL',
        hint: 'https://api.example.org/readings',
      ),
      ConsoleTextField(
        controller: _kind,
        label: 'Kind',
        hint: 'e.g. river_level',
        help: 'Tag used to match policy rules',
      ),
      const SizedBox(height: 16),
      DialogueSectionLabel('Authentication (optional)'),
      ConsoleTextField(
        controller: _authHeader,
        label: 'Header name',
        hint: 'Authorization',
      ),
      ConsoleTextField(
        controller: _authToken,
        label: 'Token',
        hint: 'Bearer ...',
        obscure: true,
        help: 'Our credential to their API, stored server-side',
      ),
      const SizedBox(height: 16),
      DialogueSectionLabel('Polling'),
      Row(
        children: [
          Expanded(
            child: ConsoleTextField(
              controller: _pollSecs,
              label: 'Poll every (s)',
              hint: '60',
              number: true,
              help: 'Minimum 1',
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: ConsoleTextField(
              controller: _maxAgeSecs,
              label: 'Max age (s)',
              hint: '900',
              number: true,
              help: '0 = no staleness check',
            ),
          ),
        ],
      ),
      const SizedBox(height: 16),
      DialogueSectionLabel('Sanity bounds'),
      Row(
        children: [
          Expanded(
            child: ConsoleTextField(
              controller: _minValue,
              label: 'Min value',
              number: true,
              help: ' ',
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: ConsoleTextField(
              controller: _maxValue,
              label: 'Max value',
              number: true,
              help: '0 disables bounds check',
            ),
          ),
        ],
      ),
    ],
  );

  /// Second column is for the field mapping JSON input.
  Widget _mappingColumn(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      DialogueSectionLabel('Field mapping'),
      Text(
        'Tells the validator where to find each value in this source\'s '
        'response.',
        style: TextStyle(
          fontSize: 12,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
      const SizedBox(height: 10),
      Container(
        decoration: BoxDecoration(
          color: _fieldMapOk
              ? theme.colorScheme.surfaceContainerHigh
              : theme.colorScheme.errorContainer,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: _fieldMapOk
                ? theme.colorScheme.outlineVariant
                : theme.colorScheme.error.withValues(alpha: 0.6),
          ),
        ),
        child: TextField(
          controller: _fieldMap,
          maxLines: 14,
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 12,
            color: theme.colorScheme.onSurface,
          ),
          decoration: InputDecoration(
            border: InputBorder.none,
            contentPadding: const EdgeInsets.all(12),
            hintText: '{ "value_path": "..." }',
            hintStyle: TextStyle(color: theme.colorScheme.onSurfaceVariant),
          ),
        ),
      ),
      const SizedBox(height: 8),

      // Allow the user to reload the template in case they get stuck.
      TextButton.icon(
        onPressed: () {
          _fieldMap.text = FieldMapValidator.template;
        },
        icon: const Icon(Icons.auto_fix_high, size: 15),
        label: const Text('Insert template'),
        style: TextButton.styleFrom(
          foregroundColor: theme.colorScheme.primary,
          padding: EdgeInsets.zero,
          alignment: Alignment.centerLeft,
        ),
      ),
      const SizedBox(height: 8),
      StatusIssueList(issues: _issues, validMessage: "Mapping is valid."),
    ],
  );
}
