import 'package:dashboard/screens/login_screen.dart';
import 'package:dashboard/services/auth_service.dart';
import 'package:dashboard/theme.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widget_previews.dart';
import 'package:provider/provider.dart';

/// Login screen preview
@Preview(name: 'Login Screen', size: Size(800, 520))
Widget previewLoginScreen() {
  final stubApi = AuthService(baseUrl: '');

  return MaterialApp(
    debugShowCheckedModeBanner: false,
    theme: buildTheme(),
    home: Scaffold(
      body: ChangeNotifierProvider<AuthService>.value(
        value: stubApi,
        child: LoginScreen(),
      ),
    ),
  );
}
