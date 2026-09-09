// Policy rule model implements the Go policy.Rule.
//
// CREATE TABLE policy_rule (
//   id                TEXT PRIMARY KEY,
//   name              TEXT NOT NULL,
//   enabled           INTEGER NOT NULL DEFAULT 1,
//   match_kind        TEXT NOT NULL DEFAULT '',
//   match_min_sev     INTEGER NOT NULL DEFAULT 0,
//   match_source_id   TEXT NOT NULL DEFAULT '',
//   actuator_id       TEXT NOT NULL,
//   target_state      TEXT NOT NULL,
//   disposition       TEXT NOT NULL,
//   priority          INTEGER NOT NULL DEFAULT 0
// );

import 'package:dashboard/model/severity.dart';
import 'package:flutter/foundation.dart';

/// Per-source policy for how a validated advisory may affect the system.
enum Disposition {
  /// An advisory acts antonymously.
  advisory('advisory', 'Advisory', 'Policy may act autonomously'),

  /// Must go to the operator for approval.
  operatorApproved(
    'operator_approved',
    'Operator approved',
    'Always needs operator approval',
  );

  /// Constructor
  const Disposition(this.wire, this.label, this.help);

  final String wire;
  final String label;
  final String help;

  /// Looks up the enum by its wire string. Defaults to `advisory` if not found.
  static Disposition fromWire(String? s) {
    return Disposition.values.firstWhere(
      (element) => element.wire == s,
      orElse: () => Disposition.advisory,
    );
  }
}

/// A rule maps a matched advisory onto an actuator command, or queues it for
/// operator approval. Evaluated in priority order (highest first) by the
/// gateway's policy engine.
@immutable
class PolicyRule {
  final String id;
  final String name;
  final bool enabled;

  /// Match criteria: an advisory must satisfy every non-empty/non-zero field
  /// set here.
  final String matchKind;
  final Severity matchMinSeverity;
  final String matchSourceId;

  /// Action taken when matched.
  final String actuatorId;
  final String targetState;

  /// If true, a match is queued for operator approval instead of acting
  /// autonomously.
  final Disposition disposition;

  /// Higher wins when multiple rules target the same actuator in one
  /// evaluation.
  final int priority;

  /// Constructor.
  const PolicyRule({
    required this.id,
    required this.name,
    required this.enabled,
    required this.matchKind,
    required this.matchMinSeverity,
    required this.matchSourceId,
    required this.actuatorId,
    required this.targetState,
    required this.disposition,
    required this.priority,
  });

  /// Create an empty rule with default values.
  factory PolicyRule.empty() => const PolicyRule(
    id: '',
    name: '',
    enabled: true,
    matchKind: '',
    matchMinSeverity: Severity.critical,
    matchSourceId: '',
    actuatorId: '',
    targetState: '',
    disposition: Disposition.advisory,
    priority: 0,
  );

  /// Create a PolicyRule from JSON. Field names match the Go json tags
  /// exactly.
  factory PolicyRule.fromJson(Map<String, dynamic> j) => PolicyRule(
    id: (j['id'] ?? '').toString(),
    name: (j['name'] ?? '').toString(),
    enabled: (j['enabled'] as bool?) ?? false,
    matchKind: (j['match_kind'] ?? '').toString(),
    matchMinSeverity: Severity.fromWire(
      (j['match_min_severity'] as num?)!.toInt(),
    ),
    matchSourceId: (j['match_source_id'] ?? '').toString(),
    actuatorId: (j['actuator_id'] ?? '').toString(),
    targetState: (j['target_state'] ?? '').toString(),
    disposition: Disposition.fromWire(j['Disposition']?.toString()),
    priority: (j['priority'] as num?)?.toInt() ?? 0,
  );

  /// Translate the rule to JSON.
  Map<String, dynamic> toJson() => {
    'id': id,
    'name': name,
    'enabled': enabled,
    'match_kind': matchKind,
    'match_min_severity': matchMinSeverity.wire,
    'match_source_id': matchSourceId,
    'actuator_id': actuatorId,
    'target_state': targetState,
    'require_operator': disposition.wire,
    'priority': priority,
  };
}
