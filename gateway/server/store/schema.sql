-- Schema is used to auto generate the Go files using sqlc, this then creates
-- the accessors required for updating data.

-- An external source to fetch data from.
CREATE TABLE external_source (
  id            TEXT PRIMARY KEY,
  name          TEXT NOT NULL,
  enabled       INTEGER NOT NULL DEFAULT 1,
  url           TEXT NOT NULL,
  auth_header   TEXT NOT NULL DEFAULT '',
  auth_token    TEXT NOT NULL DEFAULT '',
  poll_ms       INTEGER NOT NULL,
  kind          TEXT NOT NULL DEFAULT '',
  max_age_ms    INTEGER NOT NULL DEFAULT 0,
  min_value     REAL NOT NULL DEFAULT 0,
  max_value     REAL NOT NULL DEFAULT 0,
  disposition   TEXT NOT NULL DEFAULT 'advisory',
  field_map     TEXT NOT NULL DEFAULT '{}'
);

-- A physical actuator
CREATE TABLE actuator (
  id             TEXT PRIMARY KEY,
  name           TEXT NOT NULL,
  enabled        INTEGER NOT NULL DEFAULT 1,
  states         TEXT NOT NULL,
  failsafe_state TEXT NOT NULL
);

-- Rule for the policy engine.
CREATE TABLE policy_rule (
  id                TEXT PRIMARY KEY,
  name              TEXT NOT NULL,
  enabled           INTEGER NOT NULL DEFAULT 1,
  match_kind        TEXT NOT NULL DEFAULT '',
  match_min_sev     INTEGER NOT NULL DEFAULT 0,
  match_source_id   TEXT NOT NULL DEFAULT '',
  actuator_id       TEXT NOT NULL,
  target_state      TEXT NOT NULL,
  require_operator  INTEGER NOT NULL DEFAULT 0,
  priority          INTEGER NOT NULL DEFAULT 0
);

-- Audit log for tracking changes.
CREATE TABLE audit_log (
  id      INTEGER PRIMARY KEY AUTOINCREMENT,
  at      TEXT NOT NULL,
  actor   TEXT NOT NULL,
  action  TEXT NOT NULL,
  entity  TEXT NOT NULL,
  detail  TEXT NOT NULL
);

-- A target to push data to. Supabase or some third party API
CREATE TABLE egress_target (
  id            TEXT PRIMARY KEY,
  name          TEXT NOT NULL,
  enabled       INTEGER NOT NULL DEFAULT 1,
  type          TEXT NOT NULL,
  dsn           TEXT NOT NULL DEFAULT '',
  webhook_url   TEXT NOT NULL DEFAULT '',
  auth_token    TEXT NOT NULL DEFAULT '',
  min_severity  INTEGER NOT NULL DEFAULT 0
);
