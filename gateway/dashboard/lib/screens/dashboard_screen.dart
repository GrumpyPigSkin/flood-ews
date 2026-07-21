// Dashboard screen is an overview of the whole station. It gives a brief
// overview of the mean water level, the highest reading across the station and
// number of report stations. It also gives an overview of sensor state, so for
// example if a sensor is malfunctioning it will show the degraded performance.

import 'package:dashboard/model/telemetry.dart';
import 'package:dashboard/services/telemetry_source.dart';
import 'package:dashboard/theme.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// Dashboard screen what the operator is greeted with when they sign in.
/// This is the only screen served on the cloud.
class DashboardScreen extends StatelessWidget {
  /// Constructor.
  const DashboardScreen({super.key});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final repo = context.watch<TelemetrySource>();

    return Scaffold(
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(28),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _TopBar(connection: repo.connection),
              const SizedBox(height: 24),
              Expanded(child: _Headline(repo: repo)),
              const SizedBox(height: 24),
              _DegradedBanner(repo: repo),
            ],
          ),
        ),
      ),
    );
  }
}

/// Displays the title and connection status.
class _TopBar extends StatelessWidget {
  /// Connection status.
  final LinkState connection;

  /// Constructor.
  const _TopBar({required this.connection});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      children: [
        Text(
          'Flood Monitoring',
          style: TextStyle(
            fontSize: 20,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.3,
            color: theme.colorScheme.onSurface,
          ),
        ),
        const SizedBox(width: 12),
        _ConnDot(connection: connection),
      ],
    );
  }
}

/// Build the coloured small connection dot.
class _ConnDot extends StatelessWidget {
  /// The connection status.
  final LinkState connection;

  /// Constructor.
  const _ConnDot({required this.connection});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    /// Determine colour based on connection.
    final color = switch (connection) {
      LinkState.connected => theme.colorScheme.secondary,
      LinkState.connecting => theme.colorScheme.tertiary,
      LinkState.disconnected => theme.colorScheme.error,
    };
    return Container(
      width: 9,
      height: 9,
      decoration: BoxDecoration(color: color, shape: BoxShape.circle),
    );
  }
}

/// The headline item, containing average, peak, num reporting.
class _Headline extends StatelessWidget {
  /// The repo to get data from.
  final TelemetrySource repo;

  /// Constructor.
  const _Headline({required this.repo});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    // Is an alert active?
    final alert = repo.hasActiveAlert;

    // Aggregate the sensor data.
    final agg = repo.aggregate();

    // No stations yet.
    if (agg.reporting == 0) {
      return const CenteredMessage(
        icon: Icons.water_drop_outlined,
        text: 'Waiting for station data',
      );
    }

    // Colour for an alert.
    final accent = alert
        ? theme.colorScheme.error
        : theme.colorScheme.secondary;
    final meanRounded = agg.meanMm.round();

    return Container(
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: alert
              ? theme.colorScheme.error.withValues(alpha: 0.5)
              : theme.colorScheme.outlineVariant,
          width: alert ? 2 : 1,
        ),
      ),
      padding: const EdgeInsets.all(36),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Text(
            // Show alert text.
            alert ? 'FLOOD ALERT' : 'Levels normal',
            style: TextStyle(
              fontSize: 18,
              fontWeight: FontWeight.w700,
              letterSpacing: alert ? 2 : 0.5,
              color: accent,
            ),
          ),
          const SizedBox(height: 20),
          // Headline value, mean across stations.
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              Text(
                '$meanRounded',
                style: TextStyle(
                  fontFamily: monoFamily,
                  fontSize: 96,
                  fontWeight: FontWeight.w800,
                  height: 1.0,
                  color: theme.colorScheme.onSurface,
                ),
              ),
              const SizedBox(width: 10),
              Padding(
                padding: const EdgeInsets.only(bottom: 14),
                child: Text(
                  'mm',
                  style: TextStyle(
                    fontSize: 28,
                    color: theme.colorScheme.onSurfaceVariant,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          // Show the user how many sensors contributed to the mean.
          Text(
            'Mean across ${agg.reporting} reporting '
            '${agg.reporting == 1 ? "station" : "stations"}',
            style: TextStyle(
              fontSize: 14,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
          const SizedBox(height: 24),
          // Peak value across all readings.
          _PeakStat(agg: agg, alert: alert),
        ],
      ),
    );
  }
}

/// Format for the peak stat.
class _PeakStat extends StatelessWidget {
  /// Aggregated values.
  final ({double meanMm, int peakMm, StationState? peakStation, int reporting})
  agg;

  /// Is an alert active.
  final bool alert;

  /// Constructor.
  const _PeakStat({required this.agg, required this.alert});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final peak = agg.peakStation;
    if (peak == null) return const SizedBox.shrink();

    // If the peak is really far out give it a severity queue. It shouldn't be
    // too far out thanks to BZT but could be a warning something is wrong.
    final overMean = agg.peakMm - agg.meanMm;
    final Color peakColor = alert
        ? theme.colorScheme.error
        : overMean > 1500
        ? theme.colorScheme.tertiary
        : theme.colorScheme.onSurfaceVariant;

    // Sub container for the peak value.
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: peakColor.withValues(alpha: 0.4)),
      ),

      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            'PEAK',
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.5,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
          const SizedBox(width: 14),
          // The peak value.
          Text(
            '${agg.peakMm}',
            style: TextStyle(
              fontFamily: monoFamily,
              fontSize: 28,
              fontWeight: FontWeight.w700,
              color: peakColor,
              height: 1.0,
            ),
          ),
          const SizedBox(width: 4),
          Padding(
            padding: const EdgeInsets.only(bottom: 2),
            child: Text(
              'mm',
              style: TextStyle(
                fontSize: 14,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          const SizedBox(width: 14),
          // The node that reported the peak value.
          Text(
            peak.euiHex,
            style: TextStyle(
              fontFamily: monoFamily,
              fontSize: 13,
              color: theme.colorScheme.onSurfaceVariant,
            ),
          ),
        ],
      ),
    );
  }
}

/// Degraded banned is displayed if any of the nodes are showing and irregular
/// state. For example outliers, invalid, stale etc.
class _DegradedBanner extends StatelessWidget {
  /// The repo for the data.
  final TelemetrySource repo;

  /// Constructor.
  const _DegradedBanner({required this.repo});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final s = repo.freshnessSummary();

    // If data is stale.
    final degraded = s.stale + s.offline;

    // Are their any outliers.
    final outliers = repo.stations.where((st) => st.outlier).length;

    // If we don't have any degraded sensors or outliers report all is good.
    if (degraded == 0 && outliers == 0) {
      return Row(
        children: [
          Icon(
            Icons.check_circle_outline,
            size: 18,
            color: theme.colorScheme.secondary,
          ),
          SizedBox(width: 8),
          Text(
            'All stations reporting normally',
            style: TextStyle(
              color: theme.colorScheme.onSurfaceVariant,
              fontSize: 14,
            ),
          ),
        ],
      );
    }

    // Build a list of strings dependent on how issues we have.
    final parts = <String>[
      if (s.offline > 0) '${s.offline} offline',
      if (s.stale > 0) '${s.stale} stale',
      if (outliers > 0) '$outliers flagged',
    ];

    // Display the issues found.
    return ErrorBanner(
      icon: Icons.warning_amber_rounded,
      message: 'Degraded performance: ${parts.join(" · ")}',
      color: theme.colorScheme.tertiary,
    );
  }
}
