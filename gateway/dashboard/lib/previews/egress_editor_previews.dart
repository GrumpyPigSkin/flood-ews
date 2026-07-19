import 'package:dashboard/screens/egress_screen.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/theme.dart';
import 'package:dashboard/widgets/egress_editor_dialogue.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

/// Dialogue preview
@Preview(name: 'Egress Target Dialogue', size: Size(1024, 768))
Widget previewEgressTargetDialogue() {
  final stubApi = _StubEgressTargetApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const EgressEditorDialogue(),
      ),
    ),
  );
}

/// Dialogue preview
@Preview(name: 'Egress Target Screen', size: Size(1024, 768))
Widget previewEgressTargetScreen() {
  final stubApi = _StubEgressTargetApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const EgressScreen(),
      ),
    ),
  );
}

/// Dialogue stub.
class _StubEgressTargetApi extends GatewayApi {
  _StubEgressTargetApi()
    : super(
        baseUrl: "http://localhost",
        auth: AuthService(baseUrl: "http://localhost"),
      );

  @override
  Future<List<dynamic>> listTargets() async {
    return [
      {
        "ID": "tgt-01",
        "Name": "Supabase",
        "Enabled": true,
        "Type": "supabase",
        "Dsn": "supabase.com",
        "WebhookUrl": "",
        "AuthToken": "",
        "MinSeverity": 3,
      },
      {
        "ID": "tgt-02",
        "Name": "External Authority",
        "Enabled": true,
        "Type": "webhook",
        "Dsn": "",
        "WebhookUrl": "webhook.com",
        "AuthToken": "",
        "MinSeverity": 2,
      },
      {
        "ID": "tgt-03",
        "Name": "Local Government",
        "Enabled": false,
        "Type": "webhook",
        "Dsn": "",
        "WebhookUrl": "webhook.com",
        "AuthToken": "token-xyz",
        "MinSeverity": 4,
      },
    ];
  }

  @override
  Future<void> upsertTarget(Map<String, dynamic> source) async {}

  @override
  Future<void> deleteTarget(String id) async {}
}
