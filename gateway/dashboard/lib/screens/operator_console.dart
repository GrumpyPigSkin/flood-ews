// Local console with navigation rail for deep-dive screens.

import 'package:dashboard/screens/actuator_screen.dart';
import 'package:dashboard/screens/dashboard_screen.dart';
import 'package:dashboard/screens/egress_screen.dart';
import 'package:dashboard/screens/external_api_screen.dart';
import 'package:dashboard/screens/operator_queue_screen.dart';
import 'package:dashboard/screens/policy_screen.dart';
import 'package:dashboard/screens/sensor_detail_screen.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/services/operator_queue_controller.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// Operator console is the screen accessed by the operator that allows usage of
/// the deep dive screens for configuration.
class OperatorConsole extends StatefulWidget {
  /// Constructor
  const OperatorConsole({super.key});

  /// Create state for the console.
  @override
  State<OperatorConsole> createState() => _OperatorConsoleState();
}

/// The console state.
class _OperatorConsoleState extends State<OperatorConsole> {
  /// The currently selected page.
  int _selected = 0;

  /// Page titles.
  static const _titles = [
    'Dashboard',
    'Approvals',
    'Sensors',
    'Actuators',
    'Policies',
    'External APIs',
    'Egress',
  ];

  /// Screens for navigation rail, built once the controller exists.
  List<Widget>? _screens;

  /// The pending controller is initialised here rather than just in the screen
  /// so the pending count can be displayed in the nav rail to alert the
  /// operator.
  OperatorQueueController? _pendingController;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // Only build the first time the screen loads.
    if (_pendingController == null) {
      _pendingController = OperatorQueueController(context.read<GatewayApi>())
        ..load();
      _screens = [
        const DashboardScreen(),
        OperatorQueueScreen(controller: _pendingController!),
        const SensorDetailScreen(),
        const ActuatorsScreen(),
        const PolicyScreen(),
        const ExternalApiScreen(),
        const EgressScreen(),
      ];
    }
  }

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final screens = _screens;
    final pendingController = _pendingController;
    if (screens == null || pendingController == null) {
      return Scaffold(
        body: Center(
          child: CircularProgressIndicator(color: theme.colorScheme.primary),
        ),
      );
    }

    return Scaffold(
      appBar: AppBar(
        // Display the selected title.
        title: Text(_titles[_selected]),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: TextButton.icon(
              onPressed: () => context.read<AuthService>().logout(),
              icon: const Icon(Icons.logout, size: 17),
              label: const Text('Sign out'),
              style: TextButton.styleFrom(
                foregroundColor: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
        ],
      ),
      body: Row(
        children: [
          SafeArea(
            // Wrapped in an animated builder so the operator queue badge
            // doesn't require the whole screen to rebuild on update.
            child: AnimatedBuilder(
              animation: pendingController,
              builder: (context, _) => NavigationRail(
                selectedIndex: _selected,
                onDestinationSelected: (v) => setState(() => _selected = v),
                backgroundColor: theme.colorScheme.surface,
                labelType: NavigationRailLabelType.all,
                selectedIconTheme: IconThemeData(
                  color: theme.colorScheme.primary,
                ),
                selectedLabelTextStyle: TextStyle(
                  color: theme.colorScheme.primary,
                ),
                unselectedIconTheme: IconThemeData(
                  color: theme.colorScheme.onSurfaceVariant,
                ),
                unselectedLabelTextStyle: TextStyle(
                  color: theme.colorScheme.onSurfaceVariant,
                ),
                destinations: [
                  const NavigationRailDestination(
                    icon: Icon(Icons.dashboard_outlined),
                    selectedIcon: Icon(Icons.dashboard),
                    label: Text('Dashboard'),
                  ),
                  NavigationRailDestination(
                    icon: _badged(
                      pendingController.pendingCount,
                      const Icon(Icons.fact_check_outlined),
                      theme,
                    ),
                    selectedIcon: _badged(
                      pendingController.pendingCount,
                      const Icon(Icons.fact_check),
                      theme,
                    ),
                    label: const Text('Approvals'),
                  ),
                  const NavigationRailDestination(
                    icon: Icon(Icons.sensors_outlined),
                    selectedIcon: Icon(Icons.sensors),
                    label: Text('Sensors'),
                  ),
                  const NavigationRailDestination(
                    icon: Icon(Icons.settings_input_component_outlined),
                    selectedIcon: Icon(Icons.settings_input_component),
                    label: Text('Actuators'),
                  ),
                  const NavigationRailDestination(
                    icon: Icon(Icons.rule_outlined),
                    selectedIcon: Icon(Icons.rule),
                    label: Text('Policies'),
                  ),
                  const NavigationRailDestination(
                    icon: Icon(Icons.api_outlined),
                    selectedIcon: Icon(Icons.api),
                    label: Text('External'),
                  ),
                  const NavigationRailDestination(
                    icon: Icon(Icons.cloud_upload_outlined),
                    selectedIcon: Icon(Icons.cloud_upload),
                    label: Text('Egress'),
                  ),
                ],
              ),
            ),
          ),
          VerticalDivider(width: 1, color: theme.colorScheme.outlineVariant),
          // Display the selected screen.
          Expanded(
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 250),
              child: screens[_selected],
            ),
          ),
        ],
      ),
    );
  }

  /// Wrap a nav-rail icon with a small count badge when count > 0.
  Widget _badged(int count, Widget icon, ThemeData theme) {
    if (count <= 0) return icon;
    return Badge(
      backgroundColor: theme.colorScheme.tertiary,
      textColor: theme.colorScheme.surface,
      label: Text(count > 9 ? '9+' : '$count'),
      child: icon,
    );
  }
}
