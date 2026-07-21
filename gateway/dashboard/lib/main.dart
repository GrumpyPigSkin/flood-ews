import 'package:dashboard/config/app_config.dart';
import 'package:dashboard/screens/dashboard_screen.dart';
import 'package:dashboard/screens/login_screen.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/services/gateway_repository.dart';
import 'package:dashboard/services/supabase_repository.dart';
import 'package:dashboard/services/telemetry_source.dart';
import 'package:dashboard/theme.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// Main entry point.
Future<void> main() async {
  // Load the config or get defaults.
  final config = AppConfig.fromEnvironment();

  // Build the role's data source.
  final TelemetrySource source = switch (config) {
    LocalConfig(:final wsUrl) => GatewayRepository(wsUrl!),
    CloudConfig c => await SupabaseRepository.create(
      url: c.supabaseUrl,
      publishableKey: c.supabaseKey,
    ),
  };
  source.start();

  runApp(FloodEwsApp(config: config, source: source));
}

/// The main application.
class FloodEwsApp extends StatelessWidget {
  final AppConfig config;
  final TelemetrySource source;

  /// Constructor.
  const FloodEwsApp({super.key, required this.config, required this.source});

  /// Build the app.
  @override
  Widget build(BuildContext context) {
    final local = switch (config) {
      LocalConfig c => c,
      CloudConfig() => null,
    };

    // Supply the providers to the application.
    return MultiProvider(
      providers: [
        Provider<AppConfig>.value(value: config),
        ChangeNotifierProvider<TelemetrySource>.value(value: source),
        if (local != null) ...[
          ChangeNotifierProvider<AuthService>(
            create: (_) => AuthService(baseUrl: local.httpBase ?? ''),
          ),
          ProxyProvider<AuthService, GatewayApi>(
            update: (_, auth, _) =>
                GatewayApi(baseUrl: local.httpBase ?? '', auth: auth),
          ),
        ],
      ],
      // return the app.
      child: MaterialApp(
        title: 'Flood Monitoring',
        debugShowCheckedModeBanner: false,
        theme: buildTheme(),
        home: const RootShell(),
      ),
    );
  }
}

/// Switches between the public board and the local console. The console is
/// only reachable when the role allows it.
class RootShell extends StatefulWidget {
  /// Constructor.
  const RootShell({super.key});

  @override
  State<RootShell> createState() => _RootShellState();
}

class _RootShellState extends State<RootShell> {
  /// Build the UI dependent on canAccessConsole.
  @override
  Widget build(BuildContext context) {
    final canConsole = context.read<AppConfig>().canAccessConsole;
    return canConsole ? const LoginGate() : const DashboardScreen();
  }
}
