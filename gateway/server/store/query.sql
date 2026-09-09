-- name: ListSources :many
SELECT id, name, enabled, url, auth_header, auth_token, poll_ms, kind, max_age_ms, min_value, max_value, field_map
FROM external_source ORDER BY id;

-- name: UpsertSource :exec
INSERT INTO external_source (id, name, enabled, url, auth_header, auth_token, poll_ms, kind, max_age_ms, min_value, max_value, field_map)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  name=excluded.name, enabled=excluded.enabled, url=excluded.url,
  auth_header=excluded.auth_header, auth_token=excluded.auth_token,
  poll_ms=excluded.poll_ms, kind=excluded.kind, max_age_ms=excluded.max_age_ms,
  min_value=excluded.min_value, max_value=excluded.max_value, field_map=excluded.field_map;

-- name: DeleteSource :exec
DELETE FROM external_source WHERE id = ?;

-- name: ListTargets :many
SELECT id, name, enabled, type, dsn, webhook_url, auth_token, min_severity
FROM egress_target ORDER BY id;

-- name: UpsertTarget :exec
INSERT INTO egress_target (id, name, enabled, type, dsn, webhook_url, auth_token, min_severity)
VALUES (?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  name=excluded.name, enabled=excluded.enabled, type=excluded.type,
  dsn=excluded.dsn, webhook_url=excluded.webhook_url,
  auth_token=excluded.auth_token, min_severity=excluded.min_severity;

-- name: DeleteTarget :exec
DELETE FROM egress_target WHERE id = ?;

-- name: ListActuators :many
SELECT id, name, enabled, states, failsafe_state
FROM actuator ORDER BY id;

-- name: UpsertActuator :exec
INSERT INTO actuator (id, name, enabled, states, failsafe_state)
VALUES (?, ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  name=excluded.name, enabled=excluded.enabled,
  states=excluded.states, failsafe_state=excluded.failsafe_state;

-- name: DeleteActuator :exec
DELETE FROM actuator WHERE id = ?;

-- name: ListRules :many
SELECT id, name, enabled, match_kind, match_min_sev, match_source_id,
       actuator_id, target_state, disposition, priority
FROM policy_rule ORDER BY priority DESC, id;

-- name: UpsertRule :exec
INSERT INTO policy_rule (id, name, enabled, match_kind, match_min_sev, match_source_id, actuator_id, target_state, disposition, priority)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  name=excluded.name, enabled=excluded.enabled, match_kind=excluded.match_kind,
  match_min_sev=excluded.match_min_sev, match_source_id=excluded.match_source_id,
  actuator_id=excluded.actuator_id, target_state=excluded.target_state,
  disposition=excluded.disposition, priority=excluded.priority;

-- name: DeleteRule :exec
DELETE FROM policy_rule WHERE id = ?;

-- name: ListAudit :many
SELECT id, at, actor, action, entity, detail
FROM audit_log ORDER BY id DESC LIMIT ?;

-- name: InsertAuditLog :exec
INSERT INTO audit_log (at, actor, action, entity, detail)
VALUES (?, ?, ?, ?, ?);

-- name: InsertPending :exec
INSERT INTO queued_advisory (source_id, kind, severity, value, unit, observed_at, received_at, disposition, raw_json, actuator_id, target_state, rule_id)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);

-- name: ListPending :many
SELECT id, source_id, kind, severity, value, unit, observed_at, received_at, disposition, raw_json, actuator_id, target_state, rule_id, status, resolved_at, resolved_by
FROM queued_advisory WHERE status = 'pending' ORDER BY id;

-- name: GetPending :one
SELECT id, source_id, kind, severity, value, unit, observed_at, received_at, disposition, raw_json, actuator_id, target_state, rule_id, status, resolved_at, resolved_by
FROM queued_advisory WHERE id = ?;

-- name: ResolvePending :execresult
UPDATE queued_advisory SET status = ?, resolved_at = ?, resolved_by = ?
WHERE id = ? AND status = 'pending';
