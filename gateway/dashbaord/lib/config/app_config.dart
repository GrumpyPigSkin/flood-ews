// App configuration.
//
// The application has two overall states:
// Local: Running on the raspberry pi and has full functionality, and access to the websocket.
// Cloud: Running remotely and only has access to overview and supabase.

import 'package:flutter/foundation.dart';

@immutable
sealed class AppConfig {
  const AppConfig();

  /// Only the local role may open the deep-dive console with control and
  /// configuration surfaces.
  bool get canAccessConsole;

  String get roleLabel;

  /// Resolve the role and its data source from build-time defines.
  factory AppConfig.fromEnvironment() {
    const role = String.fromEnvironment('ROLE', defaultValue: 'local');

    if (role == 'cloud') {
      return const CloudConfig(
        supabaseUrl: String.fromEnvironment('SUPABASE_URL'),
        supabaseKey: String.fromEnvironment('SUPABASE_KEY'),
      );
    }

    return const LocalConfig(
      wsUrl: String.fromEnvironment(
        'WS_URL',
        defaultValue: 'ws://127.0.0.1:8081/ws',
      ),
    );
  }
}

/// Local gateway role: talks to the gateway directly over the LAN.
@immutable
final class LocalConfig extends AppConfig {
  /// The gateway's WebSocket endpoint.
  final String wsUrl;

  /// Constructor
  const LocalConfig({required this.wsUrl});

  /// Can access the deep dive pages and config.
  @override
  bool get canAccessConsole => true;

  /// Derive the HTTP url for the API from the WS URL.
  String get httpBase {
    final u = Uri.parse(wsUrl);
    final scheme = u.scheme == 'wss' ? 'https' : 'http';
    return Uri(
      scheme: scheme,
      host: u.host,
      port: u.hasPort ? u.port : null,
    ).toString();
  }

  /// Role label is always local.
  @override
  String get roleLabel => 'Local';
}

/// Public observation role: reads the Supabase read-model the gateway pushes
/// to.
@immutable
final class CloudConfig extends AppConfig {
  /// URL to supabase instance.
  final String supabaseUrl;

  /// The supabase key, restricted to read only.
  final String supabaseKey;

  /// Constructor
  const CloudConfig({required this.supabaseUrl, required this.supabaseKey});

  /// Ensure we have both the URL and key.
  bool get isConfigured => supabaseUrl.isNotEmpty && supabaseKey.isNotEmpty;

  /// Can never access deep dive data.
  @override
  bool get canAccessConsole => false;

  /// Role is always public.
  @override
  String get roleLabel => 'Public';
}
