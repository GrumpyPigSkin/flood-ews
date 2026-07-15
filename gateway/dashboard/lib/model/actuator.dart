// Actuator model implements the Go actuator spec shape.

// CREATE TABLE actuator (
//   id             TEXT PRIMARY KEY,
//   name           TEXT NOT NULL,
//   enabled        INTEGER NOT NULL DEFAULT 1,
//   states         TEXT NOT NULL,
//   failsafe_state TEXT NOT NULL
// );
//
// It also combines with the actuator state endpoint which holds the current
// state of the actuator

import 'package:flutter/foundation.dart';

@immutable
class ActuatorSpec {
  /// Mimics the spec fields.
  final String id;
  final String name;
  final bool enabled;
  final List<String> states;
  final String failsafeState;

  /// Constructor.
  const ActuatorSpec({
    required this.id,
    required this.name,
    required this.states,
    required this.failsafeState,
    required this.enabled,
  });

  /// Factory function from JSON.
  factory ActuatorSpec.fromJson(Map<String, dynamic> j) => ActuatorSpec(
    id: (j['id'] ?? '').toString(),
    name: (j['name'] ?? '').toString(),
    enabled: (j['enabled'] as bool?) ?? false,
    // Split the text into a list.
    states:
        (j['states'] as List?)?.map((e) => e.toString()).toList() ?? const [],
    failsafeState: (j['failsafe_state'] ?? '').toString(),
  );

  /// Get the actuator spec as JSON.
  Map<String, dynamic> toJson() => {
    'id': id,
    'name': name,
    'enabled': enabled,
    'states': states,
    'failsafe_state': failsafeState,
  };

  /// Copy with a new enabled state.
  ActuatorSpec copyWith({bool? enabled}) => ActuatorSpec(
    id: id,
    name: name,
    states: states,
    failsafeState: failsafeState,
    enabled: enabled ?? this.enabled,
  );
}

/// A spec joined with the current state.
/// Null current state means the Daemon doesn't know what position it's in.
@immutable
class ActuatorView {
  final ActuatorSpec spec;
  final String? currentState;

  // Constructor
  const ActuatorView({required this.spec, this.currentState});

  /// Is the state in the failsafe state/
  bool get isFailsafe =>
      currentState != null && currentState == spec.failsafeState;
}
