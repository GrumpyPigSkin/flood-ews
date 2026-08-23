// Pending advisory model implements the Go store.PendingAdvisory
//
// CREATE TABLE operator_queue (
//   id            INTEGER PRIMARY KEY AUTOINCREMENT,
//   source_id     TEXT NOT NULL,
//   kind          TEXT NOT NULL,
//   severity      INTEGER NOT NULL,
//   value         REAL NOT NULL DEFAULT 0,
//   unit          TEXT NOT NULL DEFAULT '',
//   observed_at   TEXT NOT NULL,
//   received_at   TEXT NOT NULL,
//   disposition   TEXT NOT NULL,
//   raw_json      TEXT NOT NULL DEFAULT '{}',
//   actuator_id   TEXT NOT NULL DEFAULT '',
//   target_state  TEXT NOT NULL DEFAULT '',
//   rule_id       TEXT NOT NULL DEFAULT '',
//   status        TEXT NOT NULL DEFAULT 'pending',
//   resolved_at   TEXT NOT NULL DEFAULT '',
//   resolved_by   TEXT NOT NULL DEFAULT ''
// );

import 'package:dashboard/model/severity.dart';
import 'package:flutter/foundation.dart';

/// An advisory queued for operator approval.
@immutable
class PendingAdvisory {
  final int id;
  final String sourceId;
  final String kind;
  final Severity severity;
  final double value;
  final String unit;
  final DateTime observedAt;
  final DateTime receivedAt;
  final String disposition;

  /// Action a matching rule would take once approved. Empty when none.
  final String actuatorId;
  final String targetState;
  final String ruleId;

  /// 'pending', 'approved', or 'rejected'.
  final String status;
  final DateTime? resolvedAt;
  final String resolvedBy;

  /// Constructor.
  const PendingAdvisory({
    required this.id,
    required this.sourceId,
    required this.kind,
    required this.severity,
    required this.value,
    required this.unit,
    required this.observedAt,
    required this.receivedAt,
    required this.disposition,
    required this.actuatorId,
    required this.targetState,
    required this.ruleId,
    required this.status,
    this.resolvedAt,
    required this.resolvedBy,
  });

  /// Whether a matching rule attached an actuator action to run on approval.
  bool get hasAction => actuatorId.isNotEmpty;

  /// Create a PendingAdvisory from JSON. Field names match the Go json tags.
  factory PendingAdvisory.fromJson(Map<String, dynamic> j) => PendingAdvisory(
    id: (j['id'] as num?)?.toInt() ?? 0,
    sourceId: (j['source_id'] ?? '').toString(),
    kind: (j['kind'] ?? '').toString(),
    severity: Severity.fromWire((j['severity'] as num?)?.toInt() ?? 0),
    value: (j['value'] as num?)?.toDouble() ?? 0,
    unit: (j['unit'] ?? '').toString(),
    observedAt: _parseTime(j['observed_at']),
    receivedAt: _parseTime(j['received_at']),
    disposition: (j['disposition'] ?? '').toString(),
    actuatorId: (j['actuator_id'] ?? '').toString(),
    targetState: (j['target_state'] ?? '').toString(),
    ruleId: (j['rule_id'] ?? '').toString(),
    status: (j['status'] ?? 'pending').toString(),
    resolvedAt: j['resolved_at'] == null
        ? null
        : DateTime.tryParse(j['resolved_at'].toString()),
    resolvedBy: (j['resolved_by'] ?? '').toString(),
  );

  static DateTime _parseTime(dynamic v) =>
      DateTime.tryParse((v ?? '').toString()) ??
      DateTime.fromMillisecondsSinceEpoch(0);
}
