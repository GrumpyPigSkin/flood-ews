// Actuator card, one per device.
// Shows name and current state, lets an operator pick a target state and issue
// a manual override, and exposes enable/disable in the corner.

import 'package:dashboard/widgets/confirmation_dialogue.dart';
import 'package:flutter/material.dart';

import 'package:dashboard/model/actuator.dart';
import 'package:dashboard/theme.dart';

/// The implementation of the Actuator card.
/// Each actuator needs to show:
/// - It's name, id and enable toggle.
/// - It's current state and fail-safe state.
/// - Manual override for actuator.
class ActuatorCard extends StatefulWidget {
  final ActuatorView view;
  final bool busy;
  final Future<void> Function(String targetState) onActuate;
  final Future<void> Function(bool enabled) onSetEnabled;

  const ActuatorCard({
    super.key,
    required this.view,
    required this.busy,
    required this.onActuate,
    required this.onSetEnabled,
  });

  @override
  State<ActuatorCard> createState() => _ActuatorCardState();
}

/// Build the main UI.
class _ActuatorCardState extends State<ActuatorCard> {
  String? _selected;
  ActuatorSpec get _spec => widget.view.spec;

  @override
  void initState() {
    super.initState();
    _selected = widget.view.currentState;
  }

  @override
  void didUpdateWidget(ActuatorCard old) {
    super.didUpdateWidget(old);
    // Preserve the currently selected value, if we did an update mid selection.
    if (_selected == null || !_spec.states.contains(_selected)) {
      _selected = widget.view.currentState;
    }
  }

  // Build the card.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final enabled = _spec.enabled;
    final current = widget.view.currentState;

    return Opacity(
      opacity: enabled ? 1.0 : 0.55,
      child: Container(
        decoration: BoxDecoration(
          color: theme.colorScheme.surface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(
            color: enabled
                ? theme.colorScheme.outlineVariant
                : theme.colorScheme.outlineVariant.withValues(alpha: 0.5),
          ),
        ),
        padding: const EdgeInsets.all(16),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            _header(theme, enabled),
            const SizedBox(height: 14),
            _currentStateRow(theme, current),
            const SizedBox(height: 16),
            _controls(theme, enabled),
          ],
        ),
      ),
    );
  }

  /// The header, includes the name, ID and enable button.
  Widget _header(ThemeData theme, bool enabled) {
    return ListTile(
      tileColor: Colors.transparent,
      contentPadding: EdgeInsets.zero,
      title: Text(
        _spec.name.isEmpty ? _spec.id : _spec.name,
        style: TextStyle(
          fontSize: 15,
          fontWeight: FontWeight.w600,
          color: theme.colorScheme.onSurface,
        ),
      ),
      subtitle: Text(
        _spec.id,
        style: TextStyle(
          fontFamily: monoFamily,
          fontSize: 11,
          color: theme.colorScheme.onSurfaceVariant,
        ),
      ),
      trailing: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            enabled ? 'ENABLED' : 'DISABLED',
            style: TextStyle(
              fontSize: 11,
              color: enabled
                  ? theme.colorScheme.secondary
                  : theme.colorScheme.onSurfaceVariant,
            ),
          ),
          const SizedBox(width: 4),
          Switch(
            value: enabled,
            onChanged: widget.busy
                ? null
                : (v) async {
                    final confirmed = await ConfirmDialogue.show(
                      context,
                      title: v
                          ? 'Enable ${_spec.name}?'
                          : 'Disable ${_spec.name}?',
                      body: v
                          ? 'Policy will be able to drive this actuator.'
                          : 'Policy will no longer drive this actuator.',
                      actionLabel: v ? 'Enable' : 'Disable',
                      danger: !v,
                    );
                    if (confirmed) {
                      widget.onSetEnabled(v);
                    }
                  },
            activeThumbColor: theme.colorScheme.secondary,
            inactiveThumbColor: theme.colorScheme.onSurfaceVariant,
            inactiveTrackColor: theme.colorScheme.surfaceContainerHigh,
          ),
        ],
      ),
    );
  }

  /// Current state of the actuator.
  Widget _currentStateRow(ThemeData theme, String? current) {
    final known = current != null;
    final isFailsafe = known && current == _spec.failsafeState;
    final color = !known
        ? theme.colorScheme.onSurfaceVariant
        : isFailsafe
        ? theme.colorScheme.tertiary
        : theme.colorScheme.secondary;

    return Row(
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        const SizedBox(width: 8),
        Text(
          known ? current : 'unknown',
          style: TextStyle(
            fontFamily: monoFamily,
            fontSize: 18,
            fontWeight: FontWeight.w700,
            color: color,
          ),
        ),
        if (isFailsafe) ...[
          const SizedBox(width: 8),
          Text(
            '(failsafe)',
            style: TextStyle(fontSize: 11, color: theme.colorScheme.tertiary),
          ),
        ],
        const Spacer(),
        Text(
          'failsafe: ${_spec.failsafeState}',
          style: TextStyle(
            fontSize: 11,
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
      ],
    );
  }

  /// Manual override controls.
  Widget _controls(ThemeData theme, bool enabled) {
    final canAct = enabled && !widget.busy && _selected != null;

    return Row(
      children: [
        Expanded(
          // Drop down menu for state selection.
          child: DropdownButtonFormField<String>(
            initialValue: _selected,
            isDense: true,
            dropdownColor: theme.colorScheme.surfaceContainerHigh,
            decoration: InputDecoration(
              isDense: true,
              contentPadding: const EdgeInsets.symmetric(
                horizontal: 12,
                vertical: 12,
              ),
              filled: true,
              fillColor: theme.colorScheme.surfaceContainerHigh,
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(8),
                borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
              ),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(8),
                borderSide: BorderSide(color: theme.colorScheme.outlineVariant),
              ),
            ),
            style: TextStyle(
              fontFamily: monoFamily,
              fontSize: 13,
              color: theme.colorScheme.onSurface,
            ),
            hint: Text(
              'select state',
              style: TextStyle(
                fontSize: 13,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
            // Loop over the states.
            items: [
              for (final s in _spec.states)
                DropdownMenuItem(
                  value: s,
                  child: Row(
                    children: [
                      Text(s),
                      // Mark the failsafe with a shield.
                      if (s == _spec.failsafeState) ...[
                        const SizedBox(width: 6),
                        Icon(
                          Icons.shield_outlined,
                          size: 12,
                          color: theme.colorScheme.tertiary,
                        ),
                      ],
                    ],
                  ),
                ),
            ],
            // Update the selected state.
            onChanged: enabled && !widget.busy
                ? (v) => setState(() => _selected = v)
                : null,
          ),
        ),
        const SizedBox(width: 10),
        // Override button.
        SizedBox(
          height: 42,
          child: FilledButton(
            onPressed: canAct
                ? () async {
                    final confirmed = await ConfirmDialogue.show(
                      context,
                      title: 'Override ${_spec.name}?',
                      body: 'CAUTION: This commands the actuator immediately.',
                      actionLabel: 'Override',
                      danger: true,
                    );
                    if (confirmed) {
                      widget.onActuate(_selected!);
                    }
                  }
                : null,
            style: FilledButton.styleFrom(
              backgroundColor: theme.colorScheme.primary,
              foregroundColor: theme.colorScheme.surface,
              disabledBackgroundColor: theme.colorScheme.surfaceContainerHigh,
              padding: const EdgeInsets.symmetric(horizontal: 16),
            ),
            child: widget.busy
                ? SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      color: theme.colorScheme.onSurfaceVariant,
                    ),
                  )
                : const Text('Override'),
          ),
        ),
      ],
    );
  }
}
