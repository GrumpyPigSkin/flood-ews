import 'package:collection/collection.dart';

/// Normalised severity, matching advisory.Severity's integer ordering on the
/// Go side (0=unknown ... 4=critical). MinSeverity on a target means "only
/// forward events at or above this level".
enum Severity {
  unknown(0, 'Unknown'),
  info(1, 'Info'),
  watch(2, 'Watch'),
  warning(3, 'Warning'),
  critical(4, 'Critical');

  const Severity(this.wire, this.label);
  final int wire;
  final String label;

  static Severity fromWire(int n) =>
      values.firstWhereOrNull((s) => s.wire == n) ?? Severity.unknown;
}
