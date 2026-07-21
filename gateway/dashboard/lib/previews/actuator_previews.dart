// Preview allows us to see how the widget it looks on screen without having to
// build the entire application, log-in, navigate to screen etc.
// To get the screen to render we need to seed it with some fake data.

import 'package:dashboard/theme.dart';
import 'package:dashboard/widgets/actuator_card.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

import 'package:dashboard/model/actuator.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/screens/actuator_screen.dart';

/// Mock actuators screen.
@Preview(name: 'Actuators Screen', size: Size(1024, 768))
Widget previewActuatorsScreen() {
  final stubApi = _StubGatewayApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const ActuatorsScreen(),
      ),
    ),
  );
}

/// This first preview is for when the actuator card is enabled.
@Preview(name: 'Actuator Enabled', size: Size(375, 220))
Widget previewActuatorCardEnabled() {
  return Material(
    child: Padding(
      padding: const EdgeInsets.all(16.0),
      child: ActuatorCard(
        view: ActuatorView(
          currentState: 'OPEN',
          spec: ActuatorSpec(
            id: 'actuator-01',
            name: 'Main Flood Gate',
            enabled: true,
            failsafeState: 'CLOSED',
            states: ['OPEN', 'CLOSED'],
          ),
        ),
        busy: false,
        // ignore: avoid_print
        onActuate: (target) async => print('Actuated to: $target'),
        // ignore: avoid_print
        onSetEnabled: (enabled) async => print('Set enabled: $enabled'),
      ),
    ),
  );
}

/// This second preview is for when the actuator card is disabled.
@Preview(name: 'Actuator Disabled', size: Size(375, 220))
Widget previewActuatorCardDisabled() {
  return Material(
    child: Padding(
      padding: const EdgeInsets.all(16.0),
      child: ActuatorCard(
        view: ActuatorView(
          currentState: null,
          spec: ActuatorSpec(
            id: 'actuator-02',
            name: 'Backup Flood Gate',
            enabled: false,
            failsafeState: 'CLOSED',
            states: ['OPEN', 'CLOSED'],
          ),
        ),
        busy: false,
        onActuate: (target) async {},
        onSetEnabled: (enabled) async {},
      ),
    ),
  );
}

/// Mock gateway API to supply fake data to the controller.
class _StubGatewayApi extends GatewayApi {
  _StubGatewayApi()
    : super(
        baseUrl: "http://localhost",
        auth: AuthService(baseUrl: "http://localhost"),
      );

  // Mock returns the JSON payload list of ActuatorSpecs
  @override
  Future<List<dynamic>> listActuators() async {
    return [
      {
        'id': 'gate-01',
        'name': 'Main Flood Gate',
        'enabled': true,
        'failsafe_state': 'CLOSED',
        'states': ['OPEN', 'CLOSED', 'PARTIAL'],
      },
      {
        'id': 'valve-02',
        'name': 'Auxiliary Bypass Valve',
        'enabled': true,
        'failsafe_state': 'CLOSED',
        'states': ['OPEN', 'CLOSED'],
      },
      {
        'id': 'pump-03',
        'name': 'Emergency Drainage Pump',
        'enabled': false,
        'failsafe_state': 'OFF',
        'states': ['ON', 'OFF'],
      },
    ];
  }

  /// Mock actuator states.
  @override
  Future<Map<String, dynamic>> actuatorStates() async {
    return {'gate-01': 'OPEN', 'valve-02': 'CLOSED', 'pump-03': 'OFF'};
  }

  /// Mock modifications to avoid crashes on interaction in previewer.
  @override
  Future<Map<String, dynamic>> actuate({
    required String actuatorId,
    required String targetState,
    String? reason,
  }) async {
    return {'applied': true, 'to': targetState};
  }

  @override
  Future<void> upsertActuator(Map<String, dynamic> spec) async {}
}
