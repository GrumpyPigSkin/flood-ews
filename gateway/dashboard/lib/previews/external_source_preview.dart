import 'package:dashboard/screens/external_api_screen.dart';
import 'package:dashboard/widgets/source_editor_dialogue.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/theme.dart';

/// Main screen preview
@Preview(name: 'External Sources Screen', size: Size(1024, 768))
Widget previewExternalSourcesScreen() {
  final stubApi = _StubExternalSourceApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const ExternalApiScreen(),
      ),
    ),
  );
}

/// Dialogue preview
@Preview(name: 'External Sources dialogue', size: Size(1024, 768))
Widget previewExternalSourcesDialogue() {
  final stubApi = _StubExternalSourceApi();

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: Provider<GatewayApi>.value(
        value: stubApi,
        child: const SourceEditorDialogue(),
      ),
    ),
  );
}

// Mock data for the external sources.

class _StubExternalSourceApi extends GatewayApi {
  _StubExternalSourceApi()
    : super(
        baseUrl: "http://localhost",
        auth: AuthService(baseUrl: "http://localhost"),
      );

  @override
  Future<List<dynamic>> listSources() async {
    return [
      {
        'ID': 'src-01',
        'Name': 'Primary Weather API',
        'Enabled': true,
        'Url': 'https://api.weather.local/v1/current',
        'AuthHeader': 'test',
        'AuthToken': 'test',
        'PollMs': 60000,
        'Kind': 'rainfall',
        'MaxAgeMs': 240000,
        'MinValue': 100,
        'MaxValue': 10000,
        'Disposition': 'advisory',
      },
      {
        'ID': 'src-02',
        'Name': 'Secondary Weather API',
        'Url': 'https://api.weather.local/v1/current',
        'PollMs': 60000,
        'Enabled': true,
        'Disposition': 'advisory',
      },
      {
        'ID': 'src-03',
        'Name': 'Secondary Weather API',
        'Url': 'https://api.weather.local/v1/current',
        'PollMs': 60000,
        'Enabled': false,
        'Disposition': 'advisory',
      },
    ];
  }

  @override
  Future<void> upsertSource(Map<String, dynamic> source) async {}

  @override
  Future<void> deleteSource(String id) async {}
}
