// Deep dive screen for sensor readings.
//
// This shows the full overview for a sensor rather than just an overview like
// what is on the dashboard.

import 'package:dashboard/model/telemetry.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:dashboard/widgets/sensor_level_chart.dart';
import 'package:dashboard/theme.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

class SensorDetailCard extends StatelessWidget {
  /// The current station state.
  final StationState station;

  /// Historic readings for the chart.
  final List<ReadingPoint> history;

  /// Optional flood threshold for the chart reference line.
  final int? thresholdMm;

  /// Constructor.
  const SensorDetailCard({
    super.key,
    required this.station,
    required this.history,
    this.thresholdMm,
  });

  /// Convert the validity into a color.
  Color _validityColor(BuildContext context, String v) {
    final theme = Theme.of(context);
    return switch (v) {
      'GOOD' => theme.colorScheme.secondary,
      'QUESTIONABLE' => theme.colorScheme.tertiary,
      'INVALID' => theme.colorScheme.error,
      _ => theme.colorScheme.onSurfaceVariant,
    };
  }

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final now = DateTime.now();
    final freshness = station.freshnessAt(now);
    final e = station.latest;
    final vColor = _validityColor(context, e.validity);

    // Is the sensor is a bad state.
    final critical =
        e.validity == 'INVALID' ||
        e.outlier ||
        station.alert ||
        freshness == Freshness.offline;

    return Container(
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          // If the sensor is in a critical state, outline the sensor in read.
          color: critical
              ? theme.colorScheme.error.withValues(alpha: 0.5)
              : theme.colorScheme.outlineVariant,
          width: critical ? 2 : 1,
        ),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _header(context, freshness, now),
          const SizedBox(height: 14),
          _readout(context, vColor),
          const SizedBox(height: 16),
          SensorLevelChart(history: history, thresholdMm: thresholdMm),
          const SizedBox(height: 16),
          Divider(height: 1, color: theme.colorScheme.outlineVariant),
          const SizedBox(height: 16),
          _detailGrid(context, e),
        ],
      ),
    );
  }

  /// Header contains the EUI of the sensor, whether the
  /// reading is fresh, stale or so old we consider it offline.
  /// Also displays alert and outlier flags.
  Widget _header(BuildContext context, Freshness freshness, DateTime now) {
    final theme = Theme.of(context);
    final (fLabel, fColor) = switch (freshness) {
      Freshness.fresh => ('live', theme.colorScheme.secondary),
      Freshness.stale => (_ago(now), theme.colorScheme.tertiary),
      Freshness.offline => (_ago(now), theme.colorScheme.error),
    };

    return Row(
      children: [
        Text(
          station.euiHex,
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 16,
            fontWeight: FontWeight.w700,
            color: theme.colorScheme.onSurface,
            letterSpacing: 0.5,
          ),
        ),
        const SizedBox(width: 10),
        // Draw an alert box if the sensor has detected an alert state.
        if (station.alert)
          StatusChip(label: 'ALERT', color: theme.colorScheme.error),
        // Add the outlier tag if BZT flagged this reading as an outlier.
        if (station.outlier) ...[
          const SizedBox(width: 6),
          StatusChip(label: 'OUTLIER', color: theme.colorScheme.error),
        ],
        const Spacer(),
        Container(
          width: 7,
          height: 7,
          decoration: BoxDecoration(color: fColor, shape: BoxShape.circle),
        ),
        const SizedBox(width: 6),
        // Add the freshness.
        Text(fLabel, style: TextStyle(fontSize: 12, color: fColor)),
      ],
    );
  }

  /// Display the latest reading and its validity.
  Widget _readout(BuildContext context, Color vColor) {
    final theme = Theme.of(context);
    return Row(
      crossAxisAlignment: CrossAxisAlignment.end,
      children: [
        Text(
          '${station.waterLevelMm}',
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 40,
            fontWeight: FontWeight.w800,
            color: theme.colorScheme.onSurface,
            height: 1.0,
          ),
        ),
        const SizedBox(width: 5),
        Padding(
          padding: const EdgeInsets.only(bottom: 5),
          child: Text(
            'mm',
            style: TextStyle(
              fontSize: 16,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
        ),
        const Spacer(),
        Transform.scale(
          scale: 1.5,
          alignment: Alignment.centerRight,
          child: StatusChip(label: station.validity, color: vColor),
        ),
      ],
    );
  }

  /// Display the details about the station:
  /// Via: The fog node that reported the value.
  Widget _detailGrid(BuildContext context, SensorEntry e) {
    final items = <(String, String)>[
      ('Validity code', e.validityCode.toString()),
      ('Outlier', e.outlier ? 'YES' : 'NO'),
      ('Detail byte', '0x${e.detail.toRadixString(16).padLeft(2, '0')}'),
      ('Sensor time', _sensorTime(e.timestamp)),
      ('Via', station.viaDevEui.isEmpty ? 'unknown' : station.viaDevEui),
      ('Readings', history.length.toString()),
    ];

    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        mainAxisSpacing: 12,
        crossAxisSpacing: 24,
        mainAxisExtent: 40,
      ),
      itemCount: items.length,
      itemBuilder: (context, index) {
        final (k, v) = items[index];
        return _detail(context, k, v);
      },
    );
  }

  /// The text fields for each of the detail grids items.
  Widget _detail(BuildContext context, String label, String value) {
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label.toUpperCase(),
          style: TextStyle(
            fontSize: 9,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.8,
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 3),
        Text(
          value,
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 13,
            color: theme.colorScheme.onSurface,
          ),
        ),
      ],
    );
  }

  /// Calculate how long ago the station was last seen from now.
  String _ago(DateTime now) {
    final d = now.difference(station.lastSeen);
    if (d.inMinutes < 1) return '${d.inSeconds}s ago';
    if (d.inHours < 1) return '${d.inMinutes}m ago';
    return '${d.inHours}h ago';
  }

  /// Format the last sensor time.
  String _sensorTime(int secs) {
    if (secs == 0) return '';
    final t = DateTime.fromMillisecondsSinceEpoch(secs * 1000, isUtc: true);
    return DateFormat('HH:mm:ss').format(t.toLocal());
  }
}
