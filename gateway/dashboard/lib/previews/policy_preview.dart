import 'package:dashboard/screens/policy_screen.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/services/policy_rule_controller.dart';
import 'package:dashboard/theme.dart';
import 'package:dashboard/widgets/policy_editor_dialogue.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

/// Main screen preview
@Preview(name: 'Policy Screen', size: Size(1024, 768))
Widget previewPolicyScreen() {
  final stubApi = _StubPolicyApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const PolicyScreen(),
      ),
    ),
  );
}

/// Dialogue preview
@Preview(name: 'Policy dialogue', size: Size(1024, 768))
Widget previewExternalSourcesDialogue() {
  final stubApi = _StubPolicyApi();
  final policyController = PolicyRuleController(stubApi);

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: PolicyEditorDialogue(actuators: policyController.actuators),
      ),
    ),
  );
}

class _StubPolicyApi extends GatewayApi {
  _StubPolicyApi()
    : super(
        baseUrl: "http://localhost",
        auth: AuthService(baseUrl: "http://localhost"),
      );

  @override
  Future<List<dynamic>> listRules() async {
    return [
      {
        "id": 'pump-on',
        "name": 'Pump on',
        "enabled": true,
        "match_kind": '',
        "match_min_severity": 1,
        "match_source_id": '',
        "actuator_id": 'pump-03',
        "target_state": 'ON',
        "require_operator": false,
        "priority": 10,
      },
      {
        "id": 'pump-off',
        "name": 'Pump off',
        "enabled": true,
        "match_kind": '',
        "match_min_severity": 1,
        "match_source_id": '',
        "actuator_id": 'pump-03',
        "target_state": 'OFF',
        "require_operator": false,
        "priority": 5,
      },
    ];
  }

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

  @override
  Future<void> upsertRule(Map<String, dynamic> source) async {}

  @override
  Future<void> deleteRule(String id) async {}
}
