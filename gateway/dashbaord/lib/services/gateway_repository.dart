// Local data source, websocket to the gateway over LAN.
// Only usable on the Gateway, not in the cloud.

import 'dart:async';

import 'package:dashbaord/model/telemetry.dart';
import 'package:dashbaord/services/telemetry_source.dart';
import 'package:dashbaord/services/websocket_client.dart';
import 'package:flutter/material.dart';

/// Manages the websocket connection on the gateway. Implements the
/// TelemetrySource interface.
class GatewayRepository extends ChangeNotifier
    with StationFold
    implements TelemetrySource {
  /// The websocket client.
  final WebsocketClient _client;

  /// Subscription to the uplink message.
  StreamSubscription? _uplinkSub;

  /// Subscription to state changes.
  StreamSubscription? _stateSub;

  /// Timer for freshness.
  Timer? _freshnessTicker;

  /// Recent uplinks.
  final List<Uplink> _recent = [];

  /// Maximum number of recent messages.
  static const int _maxRecent = 200;

  /// The current connection status.
  LinkState _connection = LinkState.connecting;

  /// Constructor
  GatewayRepository(String wsUrl) : _client = WebsocketClient(wsUrl);

  /// Get the current connection status.
  @override
  LinkState get connection => _connection;

  /// Get the recent uplinks.
  List<Uplink> get recent => List.unmodifiable(_recent);

  /// Set up subscription and start the websocket client.
  @override
  void start() {
    _uplinkSub = _client.uplinks.listen(_ingest);
    _stateSub = _client.state.listen((s) {
      _connection = switch (s) {
        WsState.connected => LinkState.connected,
        WsState.connecting => LinkState.connecting,
        WsState.disconnected => LinkState.disconnected,
      };
      notifyListeners();
    });

    _freshnessTicker = Timer.periodic(
      const Duration(seconds: 15),
      (_) => notifyListeners(),
    );
    _client.start();
  }

  /// Handle a new message, append it to the the recent list and prune old
  /// values.
  void _ingest(Uplink uplink) {
    _recent.insert(0, uplink);
    if (_recent.length > _maxRecent) {
      _recent.removeRange(_maxRecent, _recent.length);
    }
    foldUplink(uplink);
    notifyListeners();
  }

  /// Close the subscriptions, and the websocket client.
  @override
  void dispose() {
    _freshnessTicker?.cancel();
    _uplinkSub?.cancel();
    _stateSub?.cancel();
    _client.dispose();
    super.dispose();
  }
}
