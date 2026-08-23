import 'dart:async';

import 'package:dashboard/model/pending_advisory.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:flutter/foundation.dart';

class OperatorQueueController extends ChangeNotifier {
  /// Handle to the API
  final GatewayApi api;

  /// The interval to poll for new operator queued items.
  final Duration pollInterval;

  /// Constructor
  OperatorQueueController(
    this.api, {
    this.pollInterval = const Duration(seconds: 15),
  });

  /// List of loaded pending advisories, server order (oldest first).
  List<PendingAdvisory> _pending = const [];

  /// Are we currently loading.
  bool _loading = false;

  /// Optional error.
  String? _error;

  /// Per-id in-flight flag so other items aren't blocked.
  final Set<int> _busy = {};

  /// Timer for periodic polling.
  Timer? _ticker;

  /// Get the pending advisories.
  List<PendingAdvisory> get pending => _pending;

  /// Number of items currently awaiting a decision, for nav-rail badges.
  int get pendingCount => _pending.length;

  /// Get if we're loading.
  bool get loading => _loading;

  /// Get an optional error.
  String? get error => _error;

  /// Get if the requested `id` is busy.
  bool isBusy(int id) => _busy.contains(id);

  /// Clear the current error.
  void clearError() {
    _error = null;
    notifyListeners();
  }

  /// Load the pending list, and start polling if not already running.
  Future<void> load() async {
    _ticker ??= Timer.periodic(
      pollInterval,
      (_) => unawaited(_refresh(showLoading: false)),
    );
    await _refresh(showLoading: true);
  }

  /// Refresh callback called on each timer timeout.
  Future<void> _refresh({required bool showLoading}) async {
    if (showLoading) {
      _loading = true;
      notifyListeners();
    }
    try {
      final json = await api.listPending();
      _pending = json
          .whereType<Map<String, dynamic>>()
          .map(PendingAdvisory.fromJson)
          .toList();
      _error = null;
    } on GatewayApiException catch (e) {
      _error = e.message;
    } catch (e) {
      _error = 'Failed to load the operator queue';
    } finally {
      if (showLoading) {
        _loading = false;
      }
      notifyListeners();
    }
  }

  /// Approve an item: the gateway executes its attached action, if any.
  Future<void> approve(int id) => _resolve(id, api.approvePending);

  /// Reject an item: no actuator command is ever executed.
  Future<void> reject(int id) => _resolve(id, api.rejectPending);

  /// Shared resolve path for approve/reject: marks the id busy, calls
  /// `action`, and drops the item from the local list on success.
  Future<void> _resolve(
    int id,
    Future<Map<String, dynamic>> Function(int id) action,
  ) async {
    _busy.add(id);
    _error = null;
    notifyListeners();

    try {
      await action(id);
      _pending = _pending.where((p) => p.id != id).toList();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(id);
      notifyListeners();
    }
  }

  /// Dispose called when destructed.
  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }
}
