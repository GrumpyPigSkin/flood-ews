// External source model.
//
// CREATE TABLE IF NOT EXISTS external_source (
//   id            TEXT PRIMARY KEY,
//   name          TEXT NOT NULL,
//   enabled       INTEGER NOT NULL DEFAULT 1,
//   url           TEXT NOT NULL,
//   auth_header   TEXT NOT NULL DEFAULT '',
//   auth_token    TEXT NOT NULL DEFAULT '',
//   poll_ms       INTEGER NOT NULL,
//   kind          TEXT NOT NULL DEFAULT '',
//   max_age_ms    INTEGER NOT NULL DEFAULT 0,
//   min_value     REAL NOT NULL DEFAULT 0,
//   max_value     REAL NOT NULL DEFAULT 0,
//   field_map     TEXT NOT NULL DEFAULT '{}'
// );

import 'dart:convert';

import 'package:flutter/foundation.dart';

/// An external source maps to an external source is server Go code.
@immutable
class ExternalSource {
  final String id;
  final String name;
  final bool enabled;
  final String url;
  final String authHeader;
  final String authToken;
  final int pollMs;
  final String kind;
  final int maxAgeMs;
  final double minValue;
  final double maxValue;

  /// The FieldMapping JSON, stored opaquely server-side. This is what tells
  /// the generic validator how to map this source's response onto an
  /// advisory.
  final String fieldMap;

  const ExternalSource({
    required this.id,
    required this.name,
    required this.enabled,
    required this.url,
    this.authHeader = '',
    this.authToken = '',
    required this.pollMs,
    required this.kind,
    required this.maxAgeMs,
    required this.minValue,
    required this.maxValue,
    required this.fieldMap,
  });

  /// Create and empty source with default values.
  factory ExternalSource.empty() => const ExternalSource(
    id: '',
    name: '',
    enabled: true,
    url: '',
    pollMs: 60000,
    kind: '',
    maxAgeMs: 900000,
    minValue: 0,
    maxValue: 0,
    fieldMap: '',
  );

  /// Create an ExternalSource from JSON, the JSON names need to match the Go
  /// names exactly.
  factory ExternalSource.fromJson(Map<String, dynamic> j) => ExternalSource(
    id: (j['ID'] ?? '').toString(),
    name: (j['Name'] ?? '').toString(),
    enabled: (j['Enabled'] as bool?) ?? false,
    url: (j['Url'] ?? '').toString(),
    authHeader: (j['AuthHeader'] ?? '').toString(),
    authToken: (j['AuthToken'] ?? '').toString(),
    pollMs: (j['PollMs'] as num?)?.toInt() ?? 60000,
    kind: (j['Kind'] ?? '').toString(),
    maxAgeMs: (j['MaxAgeMs'] as num?)?.toInt() ?? 0,
    minValue: (j['MinValue'] as num?)?.toDouble() ?? 0,
    maxValue: (j['MaxValue'] as num?)?.toDouble() ?? 0,
    fieldMap: (j['FieldMap'] ?? '').toString(),
  );

  /// Translate the class to JSON.
  Map<String, dynamic> toJson() => {
    'ID': id,
    'Name': name,
    'Enabled': enabled,
    'Url': url,
    'AuthHeader': authHeader,
    'AuthToken': authToken,
    'PollMs': pollMs,
    'Kind': kind,
    'MaxAgeMs': maxAgeMs,
    'MinValue': minValue,
    'MaxValue': maxValue,
    'FieldMap': fieldMap,
  };

  /// Copy an ExternalSource with new optional values.
  ExternalSource copyWith({
    String? id,
    String? name,
    bool? enabled,
    String? url,
    String? authHeader,
    String? authToken,
    int? pollMs,
    String? kind,
    int? maxAgeMs,
    double? minValue,
    double? maxValue,
    String? fieldMap,
  }) => ExternalSource(
    id: id ?? this.id,
    name: name ?? this.name,
    enabled: enabled ?? this.enabled,
    url: url ?? this.url,
    authHeader: authHeader ?? this.authHeader,
    authToken: authToken ?? this.authToken,
    pollMs: pollMs ?? this.pollMs,
    kind: kind ?? this.kind,
    maxAgeMs: maxAgeMs ?? this.maxAgeMs,
    minValue: minValue ?? this.minValue,
    maxValue: maxValue ?? this.maxValue,
    fieldMap: fieldMap ?? this.fieldMap,
  );

  /// Get the Poll Interval as a native Duration.
  Duration get pollInterval => Duration(milliseconds: pollMs);

  /// Get the Max Age as a native Duration.
  Duration get maxAge => Duration(milliseconds: maxAgeMs);
}

/// One problem found in a FieldMapping.
@immutable
class FieldMapIssue {
  /// The error message.
  final String message;

  /// Whether or not the error was fatal.
  final bool fatal;

  /// Constructor
  const FieldMapIssue(this.message, {this.fatal = true});
}

/// Validates operator-supplied FieldMapping JSON against what the Go
/// genericValidator actually requires.
///
/// Mirrors poller/validator.go: `value_path` is mandatory the validator errors
/// if missing; severity comes from either severity_path and optional
/// `severity_map` or `severity_thresholds`, unknown severity strings silently
/// become SeverityUnknown, which is why we check them here.
class FieldMapValidator {
  /// Valid severity levels.
  static const _severities = {'info', 'watch', 'warning', 'critical'};

  /// Validate an input string the user enters is valid data and contains the
  /// fields we need to form a correct external source.
  /// Returns issues found. No fatal issues is safe to save.
  static List<FieldMapIssue> validate(String raw) {
    final issues = <FieldMapIssue>[];

    /// Ensure we have a string.
    if (raw.trim().isEmpty) {
      return [
        const FieldMapIssue(
          'Field mapping is required: the validator needs value_path to '
          'find the reading in the response.',
        ),
      ];
    }

    /// Ensure we have valid JSON.
    dynamic decoded;
    try {
      decoded = jsonDecode(raw);
    } catch (e) {
      return [FieldMapIssue('Not valid JSON: $e')];
    }

    /// Ensure it was parsed correctly.
    if (decoded is! Map<String, dynamic>) {
      return [const FieldMapIssue('Field mapping must be a JSON object.')];
    }

    // `value_path` is the one hard requirement of genericValidator.
    final valuePath = decoded['value_path'];
    if (valuePath == null ||
        (valuePath is String && valuePath.trim().isEmpty)) {
      issues.add(
        const FieldMapIssue(
          'value_path is required: without it the validator cannot extract '
          'a reading.',
        ),
      );
    } else if (valuePath is! String) {
      issues.add(const FieldMapIssue('value_path must be a string.'));
    }

    // Optional string paths must be strings if present.
    for (final key in const [
      'unit_path',
      'observed_at_path',
      'kind_path',
      'severity_path',
    ]) {
      final v = decoded[key];
      if (v != null && v is! String) {
        issues.add(FieldMapIssue('$key must be a string.'));
      }
    }

    // `severity_map` values must be known severities, else they silently become
    // SeverityUnknown in the Go validator.
    final sevMap = decoded['severity_map'];
    if (sevMap != null) {
      if (sevMap is! Map) {
        issues.add(const FieldMapIssue('severity_map must be an object.'));
      } else {
        for (final entry in sevMap.entries) {
          final val = entry.value;
          if (val is! String || !_severities.contains(val)) {
            issues.add(
              FieldMapIssue(
                'severity_map["${entry.key}"] must map to one of: '
                '${_severities.join(", ")}.',
              ),
            );
          }
        }
      }
    }

    // severity_thresholds: [{min: num, severity: ...}]
    final thresholds = decoded['severity_thresholds'];
    if (thresholds != null) {
      if (thresholds is! List) {
        issues.add(
          const FieldMapIssue('severity_thresholds must be an array.'),
        );
      } else {
        for (var i = 0; i < thresholds.length; i++) {
          final t = thresholds[i];

          /// Threshold must be a map.
          if (t is! Map) {
            issues.add(
              FieldMapIssue(
                'severity_thresholds[$i] must be an object with min and '
                'severity.',
              ),
            );
            continue;
          }
          if (t['min'] is! num) {
            issues.add(
              FieldMapIssue('severity_thresholds[$i].min must be a number.'),
            );
          }
          final sev = t['severity'];

          /// Make sure a severity is valid.
          if (sev is! String || !_severities.contains(sev)) {
            issues.add(
              FieldMapIssue(
                'severity_thresholds[$i].severity must be one of: '
                '${_severities.join(", ")}.',
              ),
            );
          }
        }
      }
    }

    // Severity is optional in Go (falls back to Unknown), but a source that
    // can never produce a severity can never match a rule with a minimum
    // severity so warn rather than block.
    final hasSevPath =
        decoded['severity_path'] is String &&
        (decoded['severity_path'] as String).trim().isNotEmpty;
    final hasThresholds = thresholds is List && thresholds.isNotEmpty;
    if (!hasSevPath && !hasThresholds) {
      issues.add(
        const FieldMapIssue(
          'No `severity_path` or `severity_thresholds`: advisories from this source '
          'will always be "unknown" severity and may not match policy rules.',
          fatal: false,
        ),
      );
    }

    return issues;
  }

  /// Validity checker, runs validator then checks for errors.
  static bool isValid(String raw) =>
      validate(raw).where((i) => i.fatal).isEmpty;

  /// Starting template.
  static const String template = '''{
  "value_path": "level_mm",
  "observed_at_path": "timestamp",
  "severity_thresholds": [
    { "min": 0,    "severity": "info" },
    { "min": 2000, "severity": "watch" },
    { "min": 4000, "severity": "warning" },
    { "min": 8000, "severity": "critical" }
  ]
}''';
}
