// Uplink and sensor data are sent over verbatim from the Go backend.
// This parses the frame into the individual components.
// Station state provides an overview for the latest data.

import 'package:flutter/foundation.dart';

/// A single sensor reading inside an uplink, with the BZT verdict baked into
/// its validity / outlier flags.
@immutable
class SensorEntry {
  /// Sensor entry data.
  final String deviceEui;
  final int waterLevelMm;
  final String validity; // "GOOD" | "QUESTIONABLE" | "INVALID" | "UNKNOWN_N"
  final int validityCode;
  final bool outlier; // BZT flagged this reading as out of consensus
  final int detail; // raw IEC 61850 detail byte
  final int timestamp; // sensor wake boundary (seconds, network time)

  /// Constructor.
  const SensorEntry({
    required this.deviceEui,
    required this.waterLevelMm,
    required this.validity,
    required this.validityCode,
    required this.outlier,
    required this.detail,
    required this.timestamp,
  });

  /// Parse a JSON payload into a sensor entry.
  factory SensorEntry.fromJson(Map<String, dynamic> j) => SensorEntry(
    deviceEui: (j['device_eui'] ?? '0').toString(),
    waterLevelMm: (j['water_level_mm'] as num?)?.toInt() ?? 0,
    validity: (j['validity'] as String?) ?? 'UNKNOWN',
    validityCode: (j['validity_code'] as num?)?.toInt() ?? -1,
    outlier: (j['outlier'] as bool?) ?? false,
    detail: (j['detail'] as num?)?.toInt() ?? 0,
    timestamp: (j['timestamp'] as num?)?.toInt() ?? 0,
  );

  /// device_eui rendered as the conventional 0xXXXX hex label.
  String get euiHex {
    try {
      return '0x${BigInt.parse(deviceEui).toRadixString(16).padLeft(4, '0')}';
    } catch (_) {
      return deviceEui;
    }
  }
}

/// A whole uplink event as forwarded by the Go bridge.
@immutable
class Uplink {
  final DateTime receivedAt;
  final String devEui;
  final String? deviceName;
  final int fCnt;
  final int? rssi;
  final double? snr;

  // Decoded telemetry payload (when present)
  final String? nodeId;
  final int? raftTerm;
  final int? raftLogIndex;
  final bool alert;
  final int count;
  final List<SensorEntry> entries;

  /// Constructor
  const Uplink({
    required this.receivedAt,
    required this.devEui,
    this.deviceName,
    required this.fCnt,
    this.rssi,
    this.snr,
    this.nodeId,
    this.raftTerm,
    this.raftLogIndex,
    required this.alert,
    required this.count,
    required this.entries,
  });

  /// Build and uplink from JSON, and uplink contains a header followed by a
  /// number of sensor readings.
  factory Uplink.fromJson(Map<String, dynamic> j) {
    final obj = j['object'] as Map<String, dynamic>?;
    final entries = <SensorEntry>[];
    // Parse the sensor entries.
    if (obj != null && obj['entries'] is List) {
      for (final e in (obj['entries'] as List)) {
        if (e is Map<String, dynamic>) {
          entries.add(SensorEntry.fromJson(e));
        }
      }
    }

    return Uplink(
      receivedAt:
          DateTime.tryParse(j['receivedAt'] as String? ?? '') ?? DateTime.now(),
      devEui: (j['devEui'] as String?) ?? '',
      deviceName: j['deviceName'] as String?,
      fCnt: (j['fCnt'] as num?)?.toInt() ?? 0,
      rssi: (j['rssi'] as num?)?.toInt(),
      snr: (j['snr'] as num?)?.toDouble(),
      nodeId: obj?['node_id']?.toString(),
      raftTerm: (obj?['raft_term'] as num?)?.toInt(),
      raftLogIndex: (obj?['raft_log_index'] as num?)?.toInt(),
      alert: (obj?['alert'] as bool?) ?? false,
      count: (obj?['count'] as num?)?.toInt() ?? 0,
      entries: entries,
    );
  }
}

/// How recently a station reported, relative to expected cadence. We need to
/// protect against a silent station.
enum Freshness { fresh, stale, offline }

/// Current state of one sensor station, folded from the latest uplink that
/// carried it.
@immutable
class StationState {
  final String deviceEui;
  final SensorEntry latest;
  final DateTime lastSeen; // when the carrying uplink was received
  final String viaDevEui; // provenance: which LoRaWAN node relayed it
  final bool alert; // uplink-level alert flag at last report

  const StationState({
    required this.deviceEui,
    required this.latest,
    required this.lastSeen,
    required this.viaDevEui,
    required this.alert,
  });

  int get waterLevelMm => latest.waterLevelMm;
  bool get outlier => latest.outlier;
  String get validity => latest.validity;
  String get euiHex => latest.euiHex;

  /// Sensors wake on a slow interval of 5 minutes, so allow 2X for stale and 4X
  /// for offline.
  static const Duration _cadence = Duration(minutes: 5);

  /// What is the freshness of the state.
  Freshness freshnessAt(DateTime now) {
    final elapsed = now.difference(lastSeen);
    if (elapsed > _cadence * 4) return Freshness.offline;
    if (elapsed > _cadence * 2) return Freshness.stale;
    return Freshness.fresh;
  }
}

/// One historical reading for a sensor, timestamped for plotting. Used for the
/// chart.
@immutable
class ReadingPoint {
  /// The time the entry was received.
  final DateTime at;

  /// The entry data.
  final SensorEntry entry;

  /// Constructor.
  const ReadingPoint({required this.at, required this.entry});

  /// Get the water level.
  int get waterLevelMm => entry.waterLevelMm;
}
