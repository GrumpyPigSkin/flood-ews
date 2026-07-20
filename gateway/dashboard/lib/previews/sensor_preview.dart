import 'dart:math';

import 'package:dashboard/screens/sensor_detail_screen.dart';
import 'package:dashboard/services/telemetry_source.dart';
import 'package:dashboard/widgets/sensor_detail_card.dart';
import 'package:dashboard/theme.dart';
import 'package:flutter/material.dart';
import 'package:dashboard/model/telemetry.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

// Generate some dummy chart data for the chart.
List<ReadingPoint> generateMockHistory(int baseLevel) {
  final now = DateTime.now();
  return List.generate(100, (i) {
    final computedLevel = Random().nextInt(200) + 800;
    return ReadingPoint(
      at: now.subtract(Duration(minutes: 20 - (i * 2))),
      entry: SensorEntry(
        deviceEui: 'DEADBEEF',
        waterLevelMm: computedLevel,
        validity: 'GOOD',
        validityCode: 0,
        outlier: Random().nextBool(),
        detail: 0x00,
        timestamp: 0,
      ),
    );
  });
}

/// Sensor card preview
@Preview(name: 'Sensor card', size: Size(800, 520))
Widget previewSensorCard() {
  final stubApi = _StubTelemetrySource();
  final now = DateTime.now();

  final station = StationState(
    deviceEui: 'DEADBEEF',
    alert: true,
    viaDevEui: 'DEADBEEF1',
    lastSeen: now.subtract(const Duration(seconds: 12)),
    latest: SensorEntry(
      deviceEui: 'DEADBEEF',
      waterLevelMm: 850,
      validity: 'GOOD',
      validityCode: 0,
      outlier: true,
      detail: 0x00,
      timestamp:
          now.subtract(const Duration(seconds: 12)).millisecondsSinceEpoch ~/
          1000,
    ),
  );

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: ChangeNotifierProvider<TelemetrySource>.value(
        value: stubApi,
        child: SensorDetailCard(
          station: station,
          history: generateMockHistory(850),
          thresholdMm: 900,
        ),
      ),
    ),
  );
}

/// Sensor card preview
@Preview(name: 'Sensor card', size: Size(800, 520))
Widget previewSensorScreen() {
  final stubApi = _StubTelemetrySource();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: ChangeNotifierProvider<TelemetrySource>.value(
        value: stubApi,
        child: SensorDetailScreen(),
      ),
    ),
  );
}

/// Mock data for the provider.
class _StubTelemetrySource extends TelemetrySource {
  _StubTelemetrySource() : super();

  @override
  void start() {}

  List<ReadingPoint> historyFor(String deviceEui) {
    return generateMockHistory(700);
  }

  ({int fresh, int stale, int offline}) freshnessSummary() {
    return (fresh: 0, stale: 0, offline: 0);
  }

  bool get hasStations => true;

  bool get hasActiveAlert => false;

  LinkState get connection => LinkState.connected;

  @override
  List<StationState> get stations => [
    StationState(
      deviceEui: 'DEADBEEF',
      alert: true,
      viaDevEui: '45829AEEFF112233',
      lastSeen: DateTime.now().subtract(const Duration(seconds: 12)),
      latest: SensorEntry(
        deviceEui: 'DEADBEEF',
        waterLevelMm: 850,
        validity: 'GOOD',
        validityCode: 0,
        outlier: false,
        detail: 0x00,
        timestamp:
            DateTime.now()
                .subtract(const Duration(seconds: 12))
                .millisecondsSinceEpoch ~/
            1000,
      ),
    ),
    StationState(
      deviceEui: 'BADF00D1',
      alert: true,
      viaDevEui: '',
      lastSeen: DateTime.now().subtract(const Duration(minutes: 45)),
      latest: SensorEntry(
        deviceEui: 'BADF00D1',
        waterLevelMm: 2100,
        validity: 'INVALID',
        validityCode: 3,
        outlier: true,
        detail: 0xFA,
        timestamp:
            DateTime.now()
                .subtract(const Duration(minutes: 45))
                .millisecondsSinceEpoch ~/
            1000,
      ),
    ),
    StationState(
      deviceEui: 'C0FFEE11',
      alert: false,
      viaDevEui: '45829AEEFF112233',
      lastSeen: DateTime.now().subtract(const Duration(hours: 3)),
      latest: SensorEntry(
        deviceEui: 'C0FFEE11',
        waterLevelMm: 350,
        validity: 'QUESTIONABLE',
        validityCode: 1,
        outlier: false,
        detail: 0x02,
        timestamp:
            DateTime.now()
                .subtract(const Duration(hours: 3))
                .millisecondsSinceEpoch ~/
            1000,
      ),
    ),
  ];
}
