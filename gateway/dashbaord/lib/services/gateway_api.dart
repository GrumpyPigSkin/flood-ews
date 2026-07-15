// Authenticated gateway API.
// Each route here targets an authenticated route on the Go server.
// This handles both the auth and the "config:write" scope.
//
// This is for local use only, the cloud build never constructs this.

import 'dart:async';
import 'dart:convert';

import 'package:dashbaord/services/auth_service.dart';
import 'package:http/http.dart' as http;

/// Special exception for API errors
class GatewayApiException implements Exception {
  final int? statusCode;
  final String message;
  GatewayApiException(this.message, {this.statusCode});
  @override
  String toString() => 'GatewayApiException($statusCode): $message';
}

class GatewayApi {
  final String baseUrl;
  final AuthService auth;
  final http.Client _http;

  GatewayApi({
    required this.baseUrl,
    required this.auth,
    http.Client? httpClient,
  }) : _http = httpClient ?? http.Client();

  /// Handle listing external sources.
  Future<List<dynamic>> listSources() => _getList('/v1/config/sources');

  /// Handle upsert-ing an external source.
  Future<void> upsertSource(Map<String, dynamic> source) =>
      _put('/v1/config/sources', source);

  /// Handle deleting an external source.
  Future<void> deleteSource(String id) => _delete('/v1/config/sources/$id');

  /// Handle listing external targets.
  Future<List<dynamic>> listTargets() => _getList('/v1/config/targets');

  /// Handle upsert-ing an external target.
  Future<void> upsertTarget(Map<String, dynamic> target) =>
      _put('/v1/config/targets', target);

  /// Handle deleting an external target.
  Future<void> deleteTarget(String id) => _delete('/v1/config/targets/$id');

  /// Handle listing actuators.
  Future<List<dynamic>> listActuators() => _getList('/v1/config/actuators');

  /// Handle upsert-ing an actuator.
  Future<void> upsertActuator(Map<String, dynamic> spec) =>
      _put('/v1/config/actuators', spec);

  /// Handle deleting an actuator.
  Future<void> deleteActuator(String id) => _delete('/v1/config/actuators/$id');

  /// Handle listing rules.
  Future<List<dynamic>> listRules() => _getList('/v1/config/rules');

  /// Handle upsert-ing a rule.
  Future<void> upsertRule(Map<String, dynamic> rule) =>
      _put('/v1/config/rules', rule);

  /// Handle deleting a rule.
  Future<void> deleteRule(String id) => _delete('/v1/config/rules/$id');

  /// Handle listing the audit log.
  Future<List<dynamic>> listAudit() => _getList('/v1/config/audit');

  /// Handle the manual actuation of an actuator, with an optional reason.
  Future<Map<String, dynamic>> actuate({
    required String actuatorId,
    required String targetState,
    String? reason,
  }) async {
    final resp = await _send(
      'POST',
      '/v1/control/actuate',
      body: {
        'actuator_id': actuatorId,
        'target_state': targetState,
        if (reason != null) 'reason': reason,
      },
    );
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// Unauthenticated readings.
  Future<List<dynamic>> readings() => _getList('/v1/dashboard/readings');

  /// Unauthenticated actuator states.
  Future<Map<String, dynamic>> actuatorStates() async {
    final resp = await _send('GET', '/v1/dashboard/actuators');
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// Handle a get request for the given `path`.
  Future<List<dynamic>> _getList(String path) async {
    final resp = await _send('GET', path);
    final decoded = jsonDecode(resp.body);
    return decoded is List ? decoded : const [];
  }

  /// Handle a PUT request for the given `path`, sending the given `body`.
  Future<void> _put(String path, Map<String, dynamic> body) =>
      _send('PUT', path, body: body);

  /// Handle a DELETE request for the given `path`.
  Future<void> _delete(String path) => _send('DELETE', path);

  /// Common CRUD send handler.
  Future<http.Response> _send(
    String method,
    String path, {
    Map<String, dynamic>? body,
  }) async {
    final uri = Uri.parse('$baseUrl$path');

    // Fill in the headers
    final headers = <String, String>{
      ...auth.authHeaders,
      if (body != null) 'Content-Type': 'application/json',
    };

    late http.Response resp;
    try {
      // Encode the body if there is one to send.
      final encoded = body == null ? null : jsonEncode(body);

      // Send the message.
      resp = await switch (method) {
        'GET' => _http.get(uri, headers: headers),
        'PUT' => _http.put(uri, headers: headers, body: encoded),
        'POST' => _http.post(uri, headers: headers, body: encoded),
        'DELETE' => _http.delete(uri, headers: headers),
        _ => throw GatewayApiException('Unsupported method $method'),
      }.timeout(const Duration(seconds: 10));
    } on TimeoutException {
      throw GatewayApiException('Gateway did not respond');
    } catch (e) {
      throw GatewayApiException('Could not reach the gateway');
    }

    if (resp.statusCode == 401) {
      // Token rejected or expired, send the user back to the login page.
      auth.onUnauthorized();
      throw GatewayApiException(
        'Not authenticated',
        statusCode: resp.statusCode,
      );
    }

    // Do not have the permission.
    if (resp.statusCode == 403) {
      throw GatewayApiException(
        'Forbidden: insufficient scope',
        statusCode: resp.statusCode,
      );
    }

    // Some other error.
    if (resp.statusCode >= 400) {
      throw GatewayApiException(
        _errorFrom(resp.body) ?? 'Gateway returned ${resp.statusCode}',
        statusCode: resp.statusCode,
      );
    }
    return resp;
  }

  /// Handle an error from the given `body`s
  static String? _errorFrom(String body) {
    try {
      final m = jsonDecode(body);
      if (m is Map && m['error'] is String) return m['error'] as String;
    } catch (_) {}
    return null;
  }

  /// Close the connection.
  void dispose() => _http.close();
}
