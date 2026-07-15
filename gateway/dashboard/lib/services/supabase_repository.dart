// Cloud datasource, reads the Supabase read-model the gateway pushes
// to. Makes no contact with the gateway.
//
// The schema in supabase:
//
// CREATE TABLE IF NOT EXISTS telemetry_event (
//   id        BIGSERIAL PRIMARY KEY,
//   at        TIMESTAMPTZ NOT NULL,
//   source    TEXT NOT NULL,
//   kind      TEXT NOT NULL,
//   severity  INTEGER NOT NULL DEFAULT 0,
//   payload   JSONB NOT NULL
// );
//
// Seeds current state with a recent-window query, then subscribes to
// realtime INSERTs so the board stays live without polling.

import 'dart:async';

import 'package:dashboard/model/telemetry.dart';
import 'package:dashboard/services/telemetry_source.dart';
import 'package:flutter/material.dart';

import 'package:supabase_flutter/supabase_flutter.dart';

/// Handle the connection and ingest of data from Supabase.
class SupabaseRepository extends ChangeNotifier
    with StationFold
    implements TelemetrySource {
  /// The Supabase connection.
  final SupabaseClient _client;

  /// The realtime channel.
  RealtimeChannel? _channel;

  /// Timer for freshness.
  Timer? _freshnessTicker;

  /// Current connection status.
  LinkState _connection = LinkState.connecting;

  /// Seed window to fetch values from up to 6 hours ago.
  static const Duration _seedWindow = Duration(hours: 6);

  /// The name of the table we are interested in.
  static const String _table = 'telemetry_event';

  /// Constructor.
  SupabaseRepository(this._client);

  /// Factory function to create a new instance.
  static Future<SupabaseRepository> create({
    required String url,
    required String publishableKey,
  }) async {
    await Supabase.initialize(url: url, publishableKey: publishableKey);
    return SupabaseRepository(Supabase.instance.client);
  }

  /// Get the current connection.
  @override
  LinkState get connection => _connection;

  /// Seed the first data start the timer.
  /// Subscribe to new inserts on Supabase.
  @override
  void start() {
    _freshnessTicker = Timer.periodic(
      const Duration(seconds: 15),
      (_) => notifyListeners(),
    );
    unawaited(_seed());
    _subscribe();
  }

  /// Get previously stored data from up to _seedWindow ago.
  Future<void> _seed() async {
    try {
      final since = DateTime.now()
          .toUtc()
          .subtract(_seedWindow)
          .toIso8601String();
      final rows = await _client
          .from(_table)
          .select()
          .gte('at', since)
          .order('at', ascending: true);

      for (final row in rows) {
        _ingestRow(row);
      }
      _connection = LinkState.connected;
      notifyListeners();
    } catch (e) {
      debugPrint('supabase seed failed: $e');
      _connection = LinkState.disconnected;
      notifyListeners();
    }
  }

  /// Subscribe to new insert commands on the Supabase table.
  void _subscribe() {
    _channel = _client
        .channel('public:$_table')
        .onPostgresChanges(
          event: PostgresChangeEvent.insert,
          schema: 'public',
          table: _table,
          callback: (payload) {
            _ingestRow(payload.newRecord);
            notifyListeners();
          },
        )
        .subscribe((status, error) {
          _connection = switch (status) {
            RealtimeSubscribeStatus.subscribed => LinkState.connected,
            RealtimeSubscribeStatus.closed ||
            RealtimeSubscribeStatus.channelError ||
            RealtimeSubscribeStatus.timedOut => LinkState.disconnected,
          };
          notifyListeners();
        });
  }

  /// Convert one telemetry_event row into an Uplink. The rows payload should
  /// already be in the format the model expects.
  void _ingestRow(Map<String, dynamic> row) {
    final payload = row['payload'];
    if (payload is! Map<String, dynamic>) return;

    final at = DateTime.tryParse(row['at']?.toString() ?? '')?.toUtc();

    final uplink = Uplink.fromJson({
      'receivedAt': (at ?? DateTime.now().toUtc()).toIso8601String(),
      'devEui': row['source']?.toString() ?? '',
      'object': payload,
    });

    if (uplink.entries.isEmpty) return;
    foldUplink(uplink);
  }

  /// Dispose of the connection.
  @override
  void dispose() {
    _freshnessTicker?.cancel();
    if (_channel != null) {
      _client.removeChannel(_channel!);
    }
    super.dispose();
  }
}
