// Actuator controller loads the specs and live states.
// Issues overrides, and toggles whether an enables.

import 'package:dashboard/model/actuator.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:flutter/foundation.dart';

class ActuatorController extends ChangeNotifier {
  /// API handle.
  final GatewayApi api;

  /// Constructor.
  ActuatorController(this.api);

  /// List of the fetched specs.
  List<ActuatorSpec> _specs = const [];

  /// Map of the fetched states.
  Map<String, String> _states = const {};

  /// Flag is we are loading the values on first run.
  bool _loading = false;

  /// Error string, could be null.
  String? _error;

  /// Per actuator in-flight flag so other actuators aren't blocked.
  final Set<String> _busy = {};

  /// Specs joined with live state, stable order by name.
  List<ActuatorView> get actuators {
    final list =
        _specs
            .map((s) => ActuatorView(spec: s, currentState: _states[s.id]))
            .toList()
          ..sort((a, b) => a.spec.name.compareTo(b.spec.name));
    return list;
  }

  /// Get the loading flag.
  bool get isLoading => _loading;

  /// Get the error message.
  String? get error => _error;

  /// Get if an actuator is currently busy.
  bool isBusy(String id) => _busy.contains(id);

  /// Load the actuator specs and their states.
  Future<void> load() async {
    _loading = true;
    _error = null;
    notifyListeners();

    try {
      // Run the awaits in parallel.
      final results = await Future.wait([
        api.listActuators(),
        api.actuatorStates(),
      ]);

      final specsJson = results[0] as List<dynamic>;
      final statesJson = results[1] as Map<String, dynamic>;

      _specs = specsJson
          .whereType<Map<String, dynamic>>()
          .map(ActuatorSpec.fromJson)
          .toList();

      _states = statesJson.map((k, v) => MapEntry(k, v.toString()));
      _error = null;
    } on GatewayApiException catch (e) {
      _error = e.message;
    } catch (e) {
      _error = 'Failed to load actuators';
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  /// Manual override, command an actuator directly. Returns the servers applied
  /// flag so false if it was already in that state.
  Future<bool> actuate(String id, String targetState, {String? reason}) async {
    _busy.add(id);
    _error = null;
    notifyListeners();
    try {
      final res = await api.actuate(
        actuatorId: id,
        targetState: targetState,
        reason: reason,
      );
      final to = res['to']?.toString();
      if (to != null) {
        _states = {..._states, id: to};
      }
      return res['applied'] == true;
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(id);
      notifyListeners();
    }
  }

  /// Update whether or not an actuator is enabled.
  Future<void> setEnable(ActuatorSpec spec, bool enabled) async {
    _busy.add(spec.id);
    _error = null;
    notifyListeners();
    try {
      final updated = spec.copyWith(enabled: enabled);
      await api.upsertActuator(updated.toJson());
      _specs = [
        for (final s in _specs)
          if (s.id == spec.id) updated else s,
      ];
      await _refreshStates();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(spec.id);
      notifyListeners();
    }
  }

  /// Refresh the actuator states after an update.
  Future<void> _refreshStates() async {
    try {
      final statesJson = await api.actuatorStates();
      _states = statesJson.map((k, v) => MapEntry(k, v.toString()));
    } catch (_) {
      // Doesn't matter card just shows unknown state.
    }
  }

  /// Clears the current error.
  void clearError() {
    if (_error != null) {
      _error = null;
    }
    notifyListeners();
  }
}
