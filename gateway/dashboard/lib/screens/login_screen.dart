// Login gate for local mode, the operator must login first before being able to do
// anything on the local dashboard.

import 'package:dashboard/screens/operator_console.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/widgets/console_field_text.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

/// The gate when the user opens the application.
class LoginGate extends StatelessWidget {
  /// Constructor.
  const LoginGate({super.key});

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final auth = context.watch<AuthService>();

    /// If logged in, take them to the operator console.
    if (auth.isLoggedIn) {
      return OperatorConsole();
    }

    return LoginScreen();
  }
}

/// Login screen, there are no users, just a global password.
class LoginScreen extends StatefulWidget {
  /// Constructor.
  const LoginScreen({super.key});

  /// Screen state.
  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

/// Login screen state.
class _LoginScreenState extends State<LoginScreen> {
  /// Controller for input field.
  final _controller = TextEditingController();

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  /// Submit the password and try login.
  Future<void> _submit() async {
    final auth = context.read<AuthService>();
    if (_controller.text.isEmpty) return;
    await auth.login(_controller.text);
    _controller.clear();
  }

  /// Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final auth = context.watch<AuthService>();

    /// Are we trying to login.
    final busy = auth.state == AuthState.authenticating;

    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 380),
          child: Container(
            margin: const EdgeInsets.all(24),
            padding: const EdgeInsets.all(28),
            decoration: BoxDecoration(
              color: theme.colorScheme.surface,
              borderRadius: BorderRadius.circular(14),
              border: Border.all(color: theme.colorScheme.outlineVariant),
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Icon(
                  Icons.lock_outline,
                  size: 34,
                  color: theme.colorScheme.primary,
                ),
                const SizedBox(height: 16),
                Text(
                  'Operator sign-in',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.w600,
                    color: theme.colorScheme.onSurface,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  'Control and configuration require authentication.',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 13,
                    color: theme.colorScheme.onSurfaceVariant,
                  ),
                ),
                // Password field.
                const SizedBox(height: 22),
                ConsoleTextField(
                  controller: _controller,
                  label: "Password",
                  hint: '••••••',
                  obscure: true,
                ),
                // Display any errors that are returned when logging in.
                if (auth.error != null) ...[
                  const SizedBox(height: 12),
                  Row(
                    children: [
                      Icon(
                        Icons.error_outline,
                        size: 16,
                        color: theme.colorScheme.error,
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          auth.error!,
                          style: TextStyle(
                            color: theme.colorScheme.error,
                            fontSize: 12,
                          ),
                        ),
                      ),
                    ],
                  ),
                ],
                const SizedBox(height: 18),
                // Login button, blocked if busy.
                FilledButton(
                  onPressed: busy ? null : _submit,
                  style: FilledButton.styleFrom(
                    backgroundColor: theme.colorScheme.primary,
                    foregroundColor: theme.scaffoldBackgroundColor,
                    padding: const EdgeInsets.symmetric(vertical: 14),
                  ),
                  child: busy
                      ? SizedBox(
                          height: 18,
                          width: 18,
                          child: CircularProgressIndicator(
                            strokeWidth: 2,
                            color: theme.scaffoldBackgroundColor,
                          ),
                        )
                      : const Text('Sign in'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
