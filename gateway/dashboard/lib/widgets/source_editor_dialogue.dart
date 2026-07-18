// Add / edit an external source.
//
// The field mapping is validated, if there is a fatal issue with the field
// mapping then the user is blocked from adding it until the issues are fixed.

import 'package:dashboard/widgets/console_field_text.dart';
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
  late Disposition _disposition;
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
    _disposition = s.disposition;
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
      disposition: _disposition,
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
            _title(theme),
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
            _actions(theme),
          ],
        ),
      ),
    );
  }

  /// Title bar at the top of the dialogue.
  Widget _title(ThemeData theme) => Padding(
    padding: const EdgeInsets.fromLTRB(20, 18, 20, 18),
    child: Row(
      children: [
        Icon(
          _isNew ? Icons.add_circle_outline : Icons.edit_outlined,
          size: 20,
          color: theme.colorScheme.primary,
        ),
        const SizedBox(width: 10),
        Text(
          _isNew ? 'Add external source' : 'Edit ${widget.existing!.id}',
          style: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w600,
            color: theme.colorScheme.onSurface,
          ),
        ),
        const Spacer(),
        // Enable button.
        Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              _enabled ? 'ENABLED' : 'DISABLED',
              style: TextStyle(
                fontSize: 11,
                color: _enabled
                    ? theme.colorScheme.secondary
                    : theme.colorScheme.onSurfaceVariant,
              ),
            ),
            Switch(
              value: _enabled,
              onChanged: (v) => setState(() => _enabled = v),
              activeThumbColor: theme.colorScheme.secondary,
              inactiveThumbColor: theme.colorScheme.onSurfaceVariant,
              inactiveTrackColor: theme.colorScheme.surfaceContainerHigh,
            ),
          ],
        ),
      ],
    ),
  );

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
      _sectionLabel('Source', theme),
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
      _sectionLabel('Authentication (optional)', theme),
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
      _sectionLabel('Polling', theme),
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
      _sectionLabel('Sanity bounds', theme),
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
      const SizedBox(height: 16),
      _sectionLabel('Disposition', theme),
      _dispositionPicker(theme),
    ],
  );

  /// Second column is for the field mapping JSON input.
  Widget _mappingColumn(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      _sectionLabel('Field mapping', theme),
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
          color: theme.colorScheme.surfaceContainerHigh,
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
          foregroundColor: theme.colorScheme.onSurfaceVariant,
          padding: EdgeInsets.zero,
          alignment: Alignment.centerLeft,
        ),
      ),
      const SizedBox(height: 8),
      StatusIssueList(issues: _issues, validMessage: "Mapping is valid."),
    ],
  );

  /// Pick the disposition.
  Widget _dispositionPicker(ThemeData theme) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      for (int i = 0; i < Disposition.values.length; i++) ...[
        if (i > 0) const SizedBox(height: 8),
        RadioListTile<Disposition>(
          value: Disposition.values[i],
          groupValue: _disposition,
          onChanged: (v) => setState(() => _disposition = v!),
          dense: true,
          activeColor: theme.colorScheme.primary,
          tileColor: theme.colorScheme.surfaceContainerHigh,
          selectedTileColor: theme.colorScheme.surfaceContainerHigh,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
          title: Text(
            // Use the name from the enumeration.
            Disposition.values[i].label,
            style: TextStyle(fontSize: 13, color: theme.colorScheme.onSurface),
          ),
          subtitle: Text(
            Disposition.values[i].help,
            style: TextStyle(
              fontSize: 11,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
        ),
      ],
    ],
  );

  /// Helper for consistent section labels.
  Widget _sectionLabel(String s, ThemeData theme) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Text(
      s.toUpperCase(),
      style: TextStyle(
        fontSize: 10,
        fontWeight: FontWeight.w700,
        letterSpacing: 1,
        color: theme.colorScheme.onSurfaceVariant,
      ),
    ),
  );

  /// Either allow the field-map to be saved and sent to the server, or block if
  /// there are any errors detected.
  Widget _actions(ThemeData theme) => Padding(
    padding: const EdgeInsets.all(16),
    child: Row(
      children: [
        if (!_fieldMapOk)
          Expanded(
            child: Text(
              'Fix the field mapping before saving',
              style: TextStyle(fontSize: 12, color: theme.colorScheme.error),
            ),
          )
        else
          const Spacer(),
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(
            'Cancel',
            style: TextStyle(color: theme.colorScheme.onSurfaceVariant),
          ),
        ),
        const SizedBox(width: 8),
        FilledButton(
          onPressed: _formOk ? _submit : null,
          style: FilledButton.styleFrom(
            backgroundColor: theme.colorScheme.primary,
            foregroundColor: theme.colorScheme.surface,
            disabledBackgroundColor: theme.colorScheme.surfaceContainerHigh,
          ),
          child: Text(_isNew ? 'Add source' : 'Save changes'),
        ),
      ],
    ),
  );
}
