// External sources controller: load, create/edit, delete.

import 'package:flutter/foundation.dart';
import 'package:collection/collection.dart';

import 'package:dashboard/model/external_source.dart';
import 'package:dashboard/services/gateway_api.dart';

/// Handles the CRUD operations for an external source.
class ExternalSourceController extends ChangeNotifier {
  /// Handle to the API.
  final GatewayApi api;

  /// Constructor.
  ExternalSourceController(this.api);

  /// List of loaded external sources.
  List<ExternalSource> _sources = const [];

  /// Are we currently loading any sources.
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

  /// Get the sources, sorted by ID for consistent rendering.
  List<ExternalSource> get sources {
    final list = [..._sources]..sort((a, b) => a.id.compareTo(b.id));
    return list;
  }

  /// The id of the currently selected item.
  String? get selectedId => _selectedId;

  /// Get the selected item.
  ExternalSource? get selected =>
      _sources.firstWhereOrNull((s) => s.id == _selectedId);

  /// Set `_selectedId` to the `id`, toggle if already selected.
  void select(String? id) {
    _selectedId = _selectedId == id ? null : id;
    notifyListeners();
  }

  /// Load the sources from the server.
  Future<void> load() async {
    _loading = true;
    _error = null;
    notifyListeners();
    try {
      final json = await api.listSources();
      _sources = json
          .whereType<Map<String, dynamic>>()
          .map(ExternalSource.fromJson)
          .toList();
      // Drop a selection that no longer exists.
      if (_selectedId != null && selected == null) _selectedId = null;
      _error = null;
    } on GatewayApiException catch (e) {
      _error = e.message;
    } catch (_) {
      _error = 'Failed to load external sources';
    } finally {
      _loading = false;
      notifyListeners();
    }
  }

  /// Create or update. Refuses locally if the field mapping has fatal issues.
  Future<void> save(ExternalSource source) async {
    final issues = FieldMapValidator.validate(
      source.fieldMap,
    ).where((i) => i.fatal).toList();
    if (issues.isNotEmpty) {
      throw GatewayApiException(issues.first.message);
    }

    _busy.add(source.id);
    _error = null;
    notifyListeners();
    try {
      // Send the new data to the server.
      await api.upsertSource(source.toJson());
      await load();
    } on GatewayApiException catch (e) {
      _error = e.message;
      rethrow;
    } finally {
      _busy.remove(source.id);
      notifyListeners();
    }
  }

  /// Delete a source that has the given `id`
  Future<void> delete(String id) async {
    _busy.add(id);
    _error = null;
    notifyListeners();
    try {
      // Try delete.
      await api.deleteSource(id);
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
