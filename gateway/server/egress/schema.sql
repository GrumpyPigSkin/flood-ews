-- Schema definition for supabase
CREATE TABLE IF NOT EXISTS telemetry_event (
  id        BIGSERIAL PRIMARY KEY,
  at        TIMESTAMPTZ NOT NULL,
  source    TEXT NOT NULL,
  kind      TEXT NOT NULL,
  severity  INTEGER NOT NULL DEFAULT 0,
  payload   JSONB NOT NULL
);
