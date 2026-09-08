// Water level over time for a single sensor.
//
// Plots the readings over time. Points are also coloured by the verdict so the
// operator can easily see what readings were questionable or invalid.
//
// On the local instance on the PI the buffer is filled in by the websocket replay.

import 'package:dashboard/model/telemetry.dart';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

/// Chart for sensor level data.
class SensorLevelChart extends StatelessWidget {
  /// Reading history.
  final List<ReadingPoint> history;

  /// Threshold line for display.
  final int? thresholdMm;

  /// Constructor.
  const SensorLevelChart({super.key, required this.history, this.thresholdMm});

  /// Convert the validity into a colour.
  static Color _validityColor(ThemeData theme, String v) => switch (v) {
    'GOOD' => theme.colorScheme.secondary,
    'QUESTIONABLE' => theme.colorScheme.tertiary,
    'INVALID' => theme.colorScheme.error,
    _ => theme.colorScheme.onSurfaceVariant,
  };

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    // Don't bother showing anything if we don't have enough readings.
    final theme = Theme.of(context);

    if (history.length < 2) {
      return SizedBox(
        height: 160,
        child: Center(
          child: Text(
            'Collecting readings...',
            style: TextStyle(
              color: theme.colorScheme.onSurfaceVariant,
              fontSize: 12,
            ),
          ),
        ),
      );
    }

    // Create the spots for the chart, and the x and y bounds.
    final t0 = history.first.at;
    final spots = <FlSpot>[];
    for (final p in history) {
      final x = p.at.difference(t0).inSeconds.toDouble();
      spots.add(FlSpot(x, p.waterLevelMm.toDouble()));
    }

    final maxX = spots.last.x;
    final (minY, maxY) = history.computeYBounds(thresholdMm: thresholdMm);

    // Build and return the chart.
    return SizedBox(
      height: 200,
      child: LineChart(
        LineChartData(
          minX: 0,
          maxX: maxX == 0 ? 1 : maxX,
          minY: minY,
          maxY: maxY,
          gridData: FlGridData(
            show: true,
            drawVerticalLine: false,
            horizontalInterval: ((maxY - minY) / 4).clamp(1, double.infinity),
            getDrawingHorizontalLine: (_) => FlLine(
              color: theme.colorScheme.outlineVariant,
              strokeWidth: 0.5,
            ),
          ),
          borderData: FlBorderData(show: false),
          titlesData: FlTitlesData(
            topTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false),
            ),
            rightTitles: const AxisTitles(
              sideTitles: SideTitles(showTitles: false),
            ),
            leftTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                reservedSize: 44,
                getTitlesWidget: (value, meta) => Text(
                  '${value.toInt()}',
                  style: TextStyle(
                    color: theme.colorScheme.onSurfaceVariant,
                    fontSize: 10,
                  ),
                ),
              ),
            ),
            bottomTitles: AxisTitles(
              sideTitles: SideTitles(
                showTitles: true,
                reservedSize: 24,
                interval: maxX == 0 ? 1 : maxX / 3,
                getTitlesWidget: (value, meta) {
                  final t = t0.add(Duration(seconds: value.toInt()));
                  return Padding(
                    padding: const EdgeInsets.only(top: 4),
                    child: Text(
                      DateFormat('HH:mm').format(t.toLocal()),
                      style: TextStyle(
                        color: theme.colorScheme.onSurfaceVariant,
                        fontSize: 10,
                      ),
                    ),
                  );
                },
              ),
            ),
          ),
          // Add the horizontal line to the chart if present.
          extraLinesData: thresholdMm != null
              ? ExtraLinesData(
                  horizontalLines: [
                    HorizontalLine(
                      y: thresholdMm!.toDouble(),
                      color: theme.colorScheme.error.withValues(alpha: 0.6),
                      strokeWidth: 1,
                      dashArray: [6, 4],
                      label: HorizontalLineLabel(
                        show: true,
                        alignment: Alignment.topRight,
                        style: TextStyle(
                          color: theme.colorScheme.error,
                          fontSize: 9,
                        ),
                        labelResolver: (_) => 'threshold',
                      ),
                    ),
                  ],
                )
              : const ExtraLinesData(),
          lineBarsData: [
            LineChartBarData(
              spots: spots,
              isCurved: false,
              color: theme.colorScheme.primary,
              barWidth: 1.6,
              dotData: FlDotData(
                show: true,
                getDotPainter: (spot, percent, barData, index) {
                  final p = history[index];
                  final v = p.entry.validity;
                  final outlier = p.entry.outlier;
                  return FlDotCirclePainter(
                    radius: outlier ? 3.5 : 2.5,
                    color: _validityColor(theme, v),
                    strokeWidth: outlier ? 1.5 : 0,
                    strokeColor: theme.colorScheme.error,
                  );
                },
              ),
              belowBarData: BarAreaData(
                show: true,
                color: theme.colorScheme.primary.withValues(alpha: 0.08),
              ),
            ),
          ],
          lineTouchData: LineTouchData(
            touchTooltipData: LineTouchTooltipData(
              getTooltipColor: (_) => theme.colorScheme.surfaceContainerHigh,
              getTooltipItems: (spots) => spots.map((s) {
                final p = history[s.spotIndex];
                return LineTooltipItem(
                  '${p.waterLevelMm} mm\n${p.entry.validity}',
                  TextStyle(
                    color: _validityColor(theme, p.entry.validity),
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                  ),
                );
              }).toList(),
            ),
          ),
        ),
      ),
    );
  }
}

/// Compute the minY/maxY values for chart bounds.
extension ReadingPointBounds on Iterable<ReadingPoint> {
  (double, double) computeYBounds({
    int? thresholdMm,
    double paddingPercent = 0.15,
  }) {
    if (isEmpty) return (0.0, 100.0);

    final levels = map((p) => p.waterLevelMm.toDouble());
    var min = levels.reduce((a, b) => a < b ? a : b);
    var max = levels.reduce((a, b) => a > b ? a : b);

    if (thresholdMm != null) {
      max = max > thresholdMm ? max : thresholdMm.toDouble();
    }

    // Guard against perfectly flat lines
    if (min == max) {
      return ((min - 50).clamp(0.0, double.infinity), max + 50);
    }

    // Add padding so the values aren't right up against the boundaries.
    final pad = ((max - min).abs() * paddingPercent).clamp(
      50.0,
      double.infinity,
    );
    return ((min - pad).clamp(0.0, double.infinity), max + pad);
  }
}
