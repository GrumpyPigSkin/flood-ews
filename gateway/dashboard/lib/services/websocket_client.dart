// Websocket client handles the websocket connection to the local Go backend.
// On connection it replays the readings from the Go backend.
// Handles automatic reconnection.

import 'dart:async';
import 'dart:convert';

import 'package:dashboard/model/telemetry.dart';
import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/status.dart' as ws_status;
import 'package:web_socket_channel/web_socket_channel.dart';

/// Enumeration for the current connection status.
enum WsState { connecting, connected, disconnected }

/// Wraps a WebSocketChannel with reconnect-on-failure. Surfaces incoming
/// Uplinks via a stream and exposes the current connection state.
class WebsocketClient {
  /// URL to connect to.
  final String url;

  /// Backoff of for reconnect attempts.
  final Duration reconnectBackoff;

  /// The websocket channel we listen on.
  WebSocketChannel? _channel;

  /// Subscription to the websocket.
  StreamSubscription? _sub;

  /// Timer for reconnection.
  Timer? _reconnectTimer;

  /// Stream for incoming data to notify other components.
  final _uplinkController = StreamController<Uplink>.broadcast();

  /// Stream for current state to notify other components.
  final _stateController = StreamController<WsState>.broadcast();

  /// The current state.
  WsState _state = WsState.disconnected;

  /// Constructor
  WebsocketClient(
    this.url, {
    this.reconnectBackoff = const Duration(seconds: 3),
  });

  Stream<Uplink> get uplinks => _uplinkController.stream;
  Stream<WsState> get state => _stateController.stream;
  WsState get currentState => _state;

  /// Set the current state, add it to the stream.
  void _setState(WsState state) {
    _state = state;
    _stateController.add(state);
  }

  /// Start tries connects to the websocket.
  void start() {
    _connect();
  }

  /// Connect to the websocket.
  void _connect() {
    _setState(WsState.connecting);

    // Connect to the websocket.
    try {
      _channel = WebSocketChannel.connect(Uri.parse(url));
    } catch (e) {
      _scheduleReconnect();
      return;
    }

    // Attach the stream.
    _sub = _channel!.stream.listen(
      _onMessage,
      onError: (e) {
        _setState(WsState.disconnected);
        _scheduleReconnect();
      },
      onDone: () {
        _setState(WsState.disconnected);
        _scheduleReconnect();
      },
      cancelOnError: true,
    );

    _setState(WsState.connected);
  }

  /// On a new message, parse the JSON and add it to our stream.
  void _onMessage(dynamic raw) {
    if (raw is! String) return;
    try {
      final j = jsonDecode(raw);
      if (j is Map<String, dynamic>) {
        _uplinkController.add(Uplink.fromJson(j));
      }
    } catch (e) {
      debugPrint('_onMessage: parse error: $e');
    }
  }

  /// Try and reconnect after `reconnectBackoff` duration.
  void _scheduleReconnect() {
    _sub?.cancel();
    _sub = null;
    try {
      _channel?.sink.close(ws_status.goingAway);
    } catch (_) {
      // Nothing we can do
    }
    _channel = null;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(reconnectBackoff, _connect);
  }

  /// Dispose of the connection and close the streams.
  void dispose() {
    _reconnectTimer?.cancel();
    _sub?.cancel();
    try {
      _channel?.sink.close(ws_status.normalClosure);
    } catch (_) {}
    _uplinkController.close();
    _stateController.close();
  }
}
