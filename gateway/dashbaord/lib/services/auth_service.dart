// Local authentication against the gateways JWT endpoint. This exists only in
// the local role.
//
// Contract with the Go package:
//   POST /v1/auth/login   {"password": "..."}  -> 200 {"token": "<jwt>"}
//                                                 401 on bad password
//   Protected routes take: Authorization: Bearer <token>
//   Token carries scope "config:write" and expires after 24h.
//
// Token is kept in memory only and not written to browser storage. The token
// issued is only for 24-hours also.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

/// Current state of AuthService
enum AuthState { loggedOut, authenticating, loggedIn, error }

/// Handle logging in and manage token.
class AuthService extends ChangeNotifier {
  /// The base url on the server.
  final String baseUrl;

  /// HTTP client handler.
  final http.Client _http;

  /// Current state.
  AuthState _state = AuthState.loggedOut;

  /// The JWT token.
  String? _token;

  /// Optional Error if one was encountered.
  String? _error;

  /// Expiry of the Token.
  DateTime? _expiresAt;

  /// Constructor.
  AuthService({required this.baseUrl, http.Client? httpClient})
    : _http = httpClient ?? http.Client();

  /// Get the current state.
  AuthState get state => _state;

  /// Get the current error or null.
  String? get error => _error;

  /// Are we currently logged in.
  bool get isLoggedIn => _state == AuthState.loggedIn && !isExpired;

  /// Has the token expired.
  bool get isExpired {
    final e = _expiresAt;
    return e != null && DateTime.now().isAfter(e);
  }

  /// Get the token if it hasn't expired.
  String? get token => isExpired ? null : _token;

  /// Get the auth headers, required a valid token.
  Map<String, String> get authHeaders =>
      token == null ? const {} : {'Authorization': 'Bearer $token'};

  /// Attempt a login with the server with the given password.
  Future<bool> login(String password) async {
    // Change state and let listeners know.
    _state = AuthState.authenticating;
    _error = null;
    notifyListeners();

    try {
      // Try and login using the given basUrl and password.
      final resp = await _http
          .post(
            Uri.parse('$baseUrl/v1/auth/login'),
            headers: const {'Content-Type': 'application/json'},
            body: jsonEncode({'password': password}),
          )
          .timeout(const Duration(seconds: 8));

      // Success, try and store the token.
      if (resp.statusCode == 200) {
        final body = jsonDecode(resp.body) as Map<String, dynamic>;
        final t = body['token'] as String?;
        if (t == null || t.isEmpty) {
          return _fail('Malformed response from gateway');
        }
        _token = t;
        // Fallback to 24 hours if we don't get a valid expiry.
        _expiresAt =
            _expiryOf(t) ?? DateTime.now().add(const Duration(hours: 24));
        _state = AuthState.loggedIn;
        _error = null;
        notifyListeners();
        return true;
      }
      // Bad password.
      if (resp.statusCode == 401) {
        return _fail('Incorrect password');
      }
      // Come other error.
      return _fail('Gateway returned ${resp.statusCode}');
    } on TimeoutException {
      // Handle a timeout.
      return _fail('Gateway did not respond');
    } catch (e) {
      // Handle other errors.
      return _fail('Could not reach the gateway');
    }
  }

  /// On logout just set the state back to null.
  void logout() {
    _token = null;
    _expiresAt = null;
    _error = null;
    _state = AuthState.loggedOut;
    notifyListeners();
  }

  /// Called by the client on a 401 so the UI drops back to the login page
  /// instead of silently failing every request.
  void onUnauthorized() {
    if (_state == AuthState.loggedIn) {
      _token = null;
      _expiresAt = null;
      _error = 'Session expired: please sign in again';
      _state = AuthState.loggedOut;
      notifyListeners();
    }
  }

  /// On fail, let listners know we have failed.
  bool _fail(String message) {
    _token = null;
    _expiresAt = null;
    _error = message;
    _state = AuthState.error;
    notifyListeners();
    return false;
  }

  /// Read the expiry of the JWT payload.
  static DateTime? _expiryOf(String jwt) {
    try {
      final parts = jwt.split('.');
      if (parts.length != 3) return null;
      final payload = utf8.decode(
        base64Url.decode(base64Url.normalize(parts[1])),
      );
      final map = jsonDecode(payload) as Map<String, dynamic>;
      final exp = map['exp'];
      if (exp is int) {
        return DateTime.fromMillisecondsSinceEpoch(exp * 1000);
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  /// Dispose of the connection.
  @override
  void dispose() {
    _http.close();
    super.dispose();
  }
}
