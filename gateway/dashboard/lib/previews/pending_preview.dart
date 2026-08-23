import 'package:dashboard/screens/operator_queue_screen.dart';
import 'package:dashboard/services/operator_queue_controller.dart';
import 'package:dashboard/theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';

/// Main screen preview
@Preview(name: 'Operator Queue Screen', size: Size(1024, 768))
Widget previewOperatorQueueScreen() {
  final stubApi = _StubOpQueueApi();
  final policyController = OperatorQueueController(stubApi);

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: OperatorQueueScreen(controller: policyController),
      ),
    ),
  );
}

class _StubOpQueueApi extends GatewayApi {
  _StubOpQueueApi()
    : super(
        baseUrl: "http://localhost",
        auth: AuthService(baseUrl: "http://localhost"),
      );

  @override
  Future<List<dynamic>> listPending() async {
    return [
      {
        "id": 1,
        "source_id": "ex-1",
        "kind": "river_level",
        "severity": 10,
        "value": 4.2,
        "unit": "mm",
        "observed_at": "10:24:15",
        "received_at": "12:44:16",
        "disposition": "advisory",
        "raw_json": "{}",
        "actuator_id": "flood-gate-1",
        "target_state": "CLOSED",
        "rule_id": "fg-rule-1",
        "status": 'pending',
        "resolved_at": "",
        "resolved_by": "",
      },
      {
        "id": 2,
        "source_id": "ex-2",
        "kind": "river_level",
        "severity": 10,
        "value": 4.2,
        "unit": "mm",
        "observed_at": "10:24:15",
        "received_at": "12:44:16",
        "disposition": "advisory",
        "raw_json": "{}",
        "actuator_id": "flood-gate-2",
        "target_state": "OPEN",
        "rule_id": "fg-rule-2",
        "status": 'pending',
        "resolved_at": "",
        "resolved_by": "",
      },
    ];
  }

  @override
  Future<Map<String, dynamic>> approvePending(int id) async {
    return {};
  }

  @override
  Future<Map<String, dynamic>> rejectPending(int id) async {
    return {};
  }
}
