// Telemetry source is the common interface for reading live data. This allows
// connections to both local websocket, on the PI only, and to supabase for the
// cloud dashboard.

import 'package:dashboard/model/telemetry.dart';
import 'package:flutter/material.dart';

/// Connection state, transport-agnostic.
enum LinkState { connecting, connected, disconnected }

/// The interface the UI depends on. Implementations are ChangeNotifiers so
/// screens can `context.watch<TelemetrySource>()`.
abstract class TelemetrySource extends ChangeNotifier {
  /// The connection state.
  LinkState get connection;

  /// Stations sorted by EUI for stable order.
  List<StationState> get stations;

  /// Are any stations present.
  bool get hasStations;

  /// True if any station is flagged alert or outlier right now.
  bool get hasActiveAlert;

  /// Count of stations in each freshness bucket.
  ({int fresh, int stale, int offline}) freshnessSummary();

  /// Begin acquiring data.
  void start();
}

/// Shared station folding and freshness logic so both implementations agree on
/// what current state and stale mean.
mixin StationFold on ChangeNotifier implements TelemetrySource {
  /// Map of stations.
  final Map<String, StationState> stationsByEui = {};

  /// Get the stations as a sorted list.
  @override
  List<StationState> get stations {
    final list = stationsByEui.values.toList()
      ..sort((a, b) => a.deviceEui.compareTo(b.deviceEui));
    return list;
  }

  /// Are any stations present.
  @override
  bool get hasStations => stationsByEui.isNotEmpty;

  /// True if any station is flagged alert or outlier right now.
  @override
  bool get hasActiveAlert =>
      stationsByEui.values.any((s) => s.alert || s.outlier);

  /// Count of stations in each freshness bucket.
  @override
  ({int fresh, int stale, int offline}) freshnessSummary() {
    final now = DateTime.now();
    var f = 0, s = 0, o = 0;
    for (final st in stationsByEui.values) {
      switch (st.freshnessAt(now)) {
        case Freshness.fresh:
          f++;
        case Freshness.stale:
          s++;
        case Freshness.offline:
          o++;
      }
    }
    return (fresh: f, stale: s, offline: o);
  }

  /// Fold one uplink's sensor entries into per-station current state.
  void foldUplink(Uplink u) {
    for (final e in u.entries) {
      stationsByEui[e.deviceEui] = StationState(
        deviceEui: e.deviceEui,
        latest: e,
        lastSeen: u.receivedAt,
        viaDevEui: u.devEui,
        alert: u.alert,
      );
    }
  }
}
