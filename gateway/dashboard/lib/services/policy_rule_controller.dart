// Policy rule controller: load, create/edit, delete.
//
// Also loads actuator specs alongside rules so the editor can offer a
// dropdown of known actuators (and their valid states) instead of a free
// text field, and so cards can show an actuator's name rather than just its
// id.

import 'package:flutter/foundation.dart';
import 'package:collection/collection.dart';

import 'package:dashboard/model/actuator.dart';
import 'package:dashboard/model/policy_rule.dart';
import 'package:dashboard/services/gateway_api.dart';

class PolicyRuleController extends ChangeNotifier {
  /// Handle to the API.
  final GatewayApi api;

  /// Constructor.
  PolicyRuleController(this.api);

  /// List of loaded rules.
  List<PolicyRule> _rules = const [];

  /// List of loaded actuator specs, for the editor's dropdown.
  List<ActuatorSpec> _actuators = const [];

  /// Are we currently loading.
  bool _loading = false;

  /// Optional error.
  String? _error;

  /// The currently selected item.
  String? _selectedId;

  /// Busy flag for each ID.
  final Set<String> _busy = {};

  /// Get if we are loading.
  bool get loading => _loading;

  /// Get an optional error.
  String? get error => _error;

  /// Get if the requested `id` is busy.
  bool isBusy(String id) => _busy.contains(id);

  /// Get the known actuators, for the editor dropdown.
  List<ActuatorSpec> get actuators => _actuators;

  /// Get the rules, sorted by priority descending then id, matching how the
  /// gateway evaluates them.
  List<PolicyRule> get rules {
    final list = [..._rules]
      ..sort((a, b) {
        final byPriority = b.priority.compareTo(a.priority);
        return byPriority != 0 ? byPriority : a.id.compareTo(b.id);
      });
    return list;
  }

  /// The id of the currently selected item.
  String? get selectedId => _selectedId;

  /// Get the selected item.
  PolicyRule? get selected =>
      _rules.firstWhereOrNull((r) => r.id == _selectedId);

  /// Look up an actuator's display name by id, falling back to the id
  /// itself when the actuator is unknown (e.g. deleted since the rule was
  /// created).
  String actuatorName(String id) {
    final spec = _actuators.firstWhereOrNull((a) => a.id == id);
    return spec == null || spec.name.isEmpty ? id : spec.name;
  }

  /// Set `_selectedId` to the `id`, toggle if already selected.
  void select(String? id) {
    _selectedId = _selectedId == id ? null : id;
    notifyListeners();
  }

  /// Load the rules and actuators from the server.
  Future<void> load() async {
    _loading = true;
    _error = null;
    notifyListeners();
    try {
      final results = await Future.wait([api.listRules(), api.listActuators()]);

      final rulesJson = results[0];
      final actuatorsJson = results[1];

      _rules = rulesJson
          .whereType<Map<String, dynamic>>()
          .map(PolicyRule.fromJson)
          .toList();
      _actuators = actuatorsJson
          .whereType<Map<String, dynamic>>()
          .map(ActuatorSpec.fromJson)
          .toList();

      // Drop a selection that no longer exists.
      if (_selectedId != null && selected == null) _selectedId = null;
      _error = null;
    } on GatewayApiException catch (e) {
      _error = e.message;
    } catch (_) {
      _error = 'Failed to load policy rules';
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  /// Create or update a rule.
  Future<void> save(PolicyRule rule) async {
    _busy.add(rule.id);
    _error = null;
    notifyListeners();
    try {
      await api.upsertRule(rule.toJson());
      await load();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(rule.id);
      notifyListeners();
    }
  }

  /// Delete a rule that has the given `id`.
  Future<void> delete(String id) async {
    _busy.add(id);
    _error = null;
    notifyListeners();
    try {
      await api.deleteRule(id);
      if (_selectedId == id) _selectedId = null;
      await load();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(id);
      notifyListeners();
    }
  }

  /// Clear the error message.
  void clearError() {
    _error = null;
    notifyListeners();
  }
}
