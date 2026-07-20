// Sensor deep-dive, one detailed card per sensor. Each card shows a chart of
// the readings over time, the validity of those readings, and the current
// sensor status.

import 'package:dashboard/services/telemetry_source.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:dashboard/widgets/sensor_detail_card.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// Build the sensor detail screen to see each sensor and it's historical data.
class SensorDetailScreen extends StatelessWidget {
  /// Constructor.
  const SensorDetailScreen({super.key});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final source = context.watch<TelemetrySource>();
    final stations = source.stations;

    /// Very simple screen just pass on the data and display the cards.
    return ScreenBody(
      intro: 'Per-station detail and level history from the gateway.',
      children: [
        if (stations.isEmpty)
          Panel(
            title: 'Stations',
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 32),
              child: Center(
                child: Column(
                  children: [
                    Icon(
                      Icons.sensors_off,
                      size: 36,
                      color: theme.colorScheme.onSurfaceVariant,
                    ),
                    const SizedBox(height: 10),
                    Text(
                      'No stations reporting yet',
                      style: TextStyle(
                        color: theme.colorScheme.onSurfaceVariant,
                        fontSize: 13,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          )
        else
          LayoutBuilder(
            builder: (context, c) {
              // Detail cards are wide, one per row on narrow, two on wide.
              final twoCol = c.maxWidth > 900;
              if (!twoCol) {
                return Column(
                  children: [
                    for (final s in stations)
                      Padding(
                        padding: const EdgeInsets.only(bottom: 16),
                        child: SensorDetailCard(
                          station: s,
                          history: source.historyFor(s.deviceEui),
                        ),
                      ),
                  ],
                );
              }
              // Two-column layout.
              final left = <Widget>[];
              final right = <Widget>[];
              for (var i = 0; i < stations.length; i++) {
                final card = Padding(
                  padding: const EdgeInsets.only(bottom: 16),
                  child: SensorDetailCard(
                    station: stations[i],
                    history: source.historyFor(stations[i].deviceEui),
                  ),
                );
                (i.isEven ? left : right).add(card);
              }
              return Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(child: Column(children: left)),
                  const SizedBox(width: 16),
                  Expanded(child: Column(children: right)),
                ],
              );
            },
          ),
      ],
    );
  }
}
