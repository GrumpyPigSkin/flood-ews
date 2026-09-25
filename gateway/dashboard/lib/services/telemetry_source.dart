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

  /// Aggregate the level across sensor stations for the dashboard.
  /// meanMm/peakMm are computed over non-offline stations. PeakStation names
  /// the highest reading. Reporting is the number of sensors that made up the
  /// aggregate.
  ({double meanMm, int peakMm, StationState? peakStation, int reporting})
  aggregate();

  /// Get the reading history for a given sensor EUI.
  List<ReadingPoint> historyFor(String deviceEui);

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

  /// List of sensor history, mapped by EUI.
  final Map<String, List<ReadingPoint>> _historyByEui = {};

  /// Max history to display on the chart.
  static const int _maxHistoryPerStation = 100;

  /// Get the reading history for a given sensor EUI.
  @override
  List<ReadingPoint> historyFor(String deviceEui) =>
      List.unmodifiable(_historyByEui[deviceEui] ?? const []);

  /// Aggregate readings into a mean, peak and some additional data for the
  /// dashboard screen.
  @override
  ({double meanMm, int peakMm, StationState? peakStation, int reporting})
  aggregate() {
    final now = DateTime.now();

    // Is the reading fresh.
    final activeStations = stationsByEui.values.where(
      (s) => s.freshnessAt(now) != Freshness.offline && s.outlier == false,
    );

    var sum = 0;
    var count = 0;
    StationState? peak;
    // Iterate over each sensor station, get the peak value across them,
    // accumulate the water level.
    for (final s in activeStations) {
      sum += s.waterLevelMm;
      count++;
      if (peak == null || s.waterLevelMm > peak.waterLevelMm) {
        peak = s;
      }
    }

    // If there are no sensors reporting return all 0's
    if (count == 0) {
      return (meanMm: 0.0, peakMm: 0, peakStation: null, reporting: 0);
    }

    // Calculate the mean and return.
    return (
      meanMm: sum / count,
      peakMm: peak!.waterLevelMm,
      peakStation: peak,
      reporting: count,
    );
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
  /// Store the sensor data into a per-sensor-data list.
  void foldUplink(Uplink u) {
    for (final e in u.entries) {
      stationsByEui[e.deviceEui] = StationState(
        deviceEui: e.deviceEui,
        latest: e,
        lastSeen: u.receivedAt,
        viaDevEui: u.devEui,
        alert: u.alert,
      );

      /// Fill in the history.
      final hist = _historyByEui.putIfAbsent(e.deviceEui, () => []);
      hist.add(ReadingPoint(at: u.receivedAt, entry: e));
      if (hist.length > _maxHistoryPerStation) {
        hist.removeRange(0, hist.length - _maxHistoryPerStation);
      }
    }
  }
}
