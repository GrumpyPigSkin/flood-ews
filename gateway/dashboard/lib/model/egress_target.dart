// Egress target model.
//
// Mirrors the sqlc generated code which like external source has no json tags.
//
// An egress target is somewhere the gateway pushes to:
// supabase: push to a supabase instance to store data.
// webhook: POST significant events to a EWS authority for regional coordination.
//
// Supabase needs a DSN and webhook might need an auth token.

import 'package:dashboard/model/severity.dart';
import 'package:dashboard/model/validator_issue.dart';
import 'package:flutter/foundation.dart';
import 'package:collection/collection.dart';

/// Egress destination type. Determines which fields are required.
enum EgressType {
  supabase('supabase', 'Supabase', 'Append telemetry to the cloud read-model'),
  webhook('webhook', 'Webhook', 'POST alerts to a central EWS authority');

  const EgressType(this.wire, this.label, this.help);
  final String wire;
  final String label;
  final String help;

  static EgressType? fromWire(String? s) =>
      values.firstWhereOrNull((e) => e.wire == s);
}

@immutable
class EgressTarget {
  final String id;
  final String name;
  final bool enabled;
  final EgressType? type;
  final int minSeverity;

  /// Whether or not the secrets fields are set on the target.
  /// Once set we don't need these secrets back.
  final bool hasDsn;
  final bool hasWebhookUrl;
  final bool hasAuthToken;

  /// The non-secret webhook URL is safe to show; the DSN and token are not.
  final String webhookUrl;

  /// Constructor.
  const EgressTarget({
    required this.id,
    required this.name,
    required this.enabled,
    required this.type,
    required this.minSeverity,
    required this.webhookUrl,
    required this.hasDsn,
    required this.hasWebhookUrl,
    required this.hasAuthToken,
  });

  /// Get the severity level.
  Severity get minSeverityLevel => Severity.fromWire(minSeverity);

  /// Get an empty severity.
  factory EgressTarget.empty() => const EgressTarget(
    id: '',
    name: '',
    enabled: true,
    type: EgressType.webhook,
    minSeverity: 3, // Default to warning.
    webhookUrl: '',
    hasDsn: false,
    hasWebhookUrl: false,
    hasAuthToken: false,
  );

  /// Create an Egress Target from JSON. Field names match the Go struct. Secret
  /// values are only checked for presence and then dropped.
  factory EgressTarget.fromJson(Map<String, dynamic> j) {
    final dsn = (j['Dsn'] ?? '').toString();
    final webhook = (j['WebhookUrl'] ?? '').toString();
    final token = (j['AuthToken'] ?? '').toString();
    return EgressTarget(
      id: (j['ID'] ?? '').toString(),
      name: (j['Name'] ?? '').toString(),
      enabled: (j['Enabled'] as bool?) ?? false,
      type: EgressType.fromWire(j['Type']?.toString()),
      minSeverity: (j['MinSeverity'] as num?)?.toInt() ?? 0,
      webhookUrl: webhook,
      hasDsn: dsn.isNotEmpty,
      hasWebhookUrl: webhook.isNotEmpty,
      hasAuthToken: token.isNotEmpty,
    );
  }

  /// Build the payload to send back to the server. Secrets are only included
  /// when the operator supplied them.
  Map<String, dynamic> toJson({String? dsn, String? authToken}) => {
    'ID': id,
    'Name': name,
    'Enabled': enabled,
    'Type': type?.wire ?? '',
    'Dsn': dsn ?? '',
    'WebhookUrl': webhookUrl,
    'AuthToken': authToken ?? '',
    'MinSeverity': minSeverity,
  };

  /// Copy and egress target with new optional values.
  EgressTarget copyWith({
    String? id,
    String? name,
    bool? enabled,
    EgressType? type,
    int? minSeverity,
    String? webhookUrl,
    bool? hasDsn,
    bool? hasWebhookUrl,
    bool? hasAuthToken,
  }) => EgressTarget(
    id: id ?? this.id,
    name: name ?? this.name,
    enabled: enabled ?? this.enabled,
    type: type ?? this.type,
    minSeverity: minSeverity ?? this.minSeverity,
    webhookUrl: webhookUrl ?? this.webhookUrl,
    hasDsn: hasDsn ?? this.hasDsn,
    hasWebhookUrl: hasWebhookUrl ?? this.hasWebhookUrl,
    hasAuthToken: hasAuthToken ?? this.hasAuthToken,
  );
}

/// Validation that matches the Go validateTarget. ID is required and the
/// token/DSN for the type of target being validated.
class EgressTargetValidator {
  /// Validate the egress target. Returns a list of issues if any are found.
  static List<ValidatorIssue> validate(
    EgressTarget t, {
    required bool dsnProvided,
    required bool urlProvided,
  }) {
    // Check ID.
    final errors = <ValidatorIssue>[];
    if (t.id.trim().isEmpty) {
      errors.add(const ValidatorIssue('ID is required.'));
    }
    // Check type.
    if (t.type == null) {
      errors.add(const ValidatorIssue('Type must be supabase or webhook.'));
      return errors;
    }
    switch (t.type!) {
      // Supabase needs DSN.
      case EgressType.supabase:
        if (!dsnProvided) {
          errors.add(
            const ValidatorIssue(
              'A Supabase target needs a database connection string.',
            ),
          );
        }
      // Webhook needs a url.
      case EgressType.webhook:
        if (!urlProvided) {
          errors.add(const ValidatorIssue('A webhook target needs a URL.'));
        }
    }
    // Validate severity.
    if (t.minSeverity < 0 || t.minSeverity > 4) {
      errors.add(
        const ValidatorIssue('Minimum severity must be between 0 and 4.'),
      );
    }
    return errors;
  }
}
