-- name: AppendEvent :exec
INSERT INTO telemetry_event (at, source, kind, severity, payload)
VALUES ($1, $2, $3, $4, $5);

-- name: AppendEventBatch :copyfrom
INSERT INTO telemetry_event (at, source, kind, severity, payload)
VALUES ($1, $2, $3, $4, $5);
