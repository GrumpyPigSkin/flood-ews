// Egress target controller handles CRUD operations for Egress Targets on the gateway.

import 'package:collection/collection.dart';
import 'package:flutter/foundation.dart';

import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/model/egress_target.dart';

class EgressTargetController extends ChangeNotifier {
  /// Handle the the gatway API.
  final GatewayApi api;

  /// Constructor.
  EgressTargetController(this.api);

  /// Targets loaded from the server.
  List<EgressTarget> _targets = [];

  /// Are we currently loading.
  bool _loading = false;

  /// An optional error string.
  String? _error;

  /// The currently selected Target ID.
  String? _selectedId;

  /// Set currently busy Targets.
  final Set<String> _busy = {};

  /// Are we loading.
  bool get loading => _loading;

  /// Get the optional error string.
  String? get error => _error;

  /// Is the given `id` busy.
  bool isBusy(String id) => _busy.contains(id);

  /// The currently selected ID.
  String? get selectedId => _selectedId;

  /// Get a sorted list of targets.
  List<EgressTarget> get targets =>
      [..._targets]..sort((a, b) => a.id.compareTo(b.id));

  /// Get the selected egress target.
  EgressTarget? get selected =>
      _targets.firstWhereOrNull((s) => s.id == _selectedId);

  /// Select the `id` if already selected toggle.
  void select(String? id) {
    _selectedId = _selectedId == id ? null : id;
    notifyListeners();
  }

  /// Load the targets from the server.
  Future<void> load() async {
    _loading = true;
    _error = null;
    notifyListeners();
    try {
      final json = await api.listTargets();
      _targets = json
          .whereType<Map<String, dynamic>>()
          .map(EgressTarget.fromJson)
          .toList();
      if (_selectedId != null && selected == null) _selectedId = null;
      _error = null;
    } on GatewayApiException catch (e) {
      _error = e.message;
    } catch (_) {
      _error = 'Failed to load egress targets';
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  /// Create or update a Target. Validation mirrors the server so we get an
  /// error here rather than waiting to the server to respond.
  Future<void> save(
    EgressTarget target, {
    String? dsn,
    String? authToken,
  }) async {
    final errors = EgressTargetValidator.validate(
      target,
      dsnProvided: (dsn ?? '').isNotEmpty,
      urlProvided: target.webhookUrl.isNotEmpty,
    );

    if (errors.isNotEmpty) {
      throw GatewayApiException(errors.first.message);
    }

    _busy.add(target.id);
    _error = null;
    notifyListeners();
    try {
      await api.upsertTarget(target.toJson(dsn: dsn, authToken: authToken));
      await load();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(target.id);
      notifyListeners();
    }
  }

  /// Delete a target with the given 'id'.
  Future<void> delete(String id) async {
    _busy.add(id);
    _error = null;
    notifyListeners();
    try {
      await api.deleteTarget(id);
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

  /// Clear the error string.
  void clearError() {
    _error = null;
    notifyListeners();
  }
}
