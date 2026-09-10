package egress

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"server/store"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

type SupabaseSink struct {
	pool    *pgxpool.Pool
	queries *Queries
	log     *slog.Logger
}

// Open the database connection using the given DSN.
func OpenSupabase(ctx context.Context, dsn string, log *slog.Logger) (*SupabaseSink, error) {
	cfg, err := pgxpool.ParseConfig(dsn)
	if err != nil {
		return nil, fmt.Errorf("parse dsn: %w", err)
	}
	cfg.MaxConns = 4

	// Use the simple protocol for maximum compatibility as I was getting
	// ENOTFOUND failure on connection.
	cfg.ConnConfig.DefaultQueryExecMode = pgx.QueryExecModeSimpleProtocol

	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("connect pool: %w", err)
	}

	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("ping: %w", err)
	}

	const maxRetries = 5
	backoff := 1 * time.Second

	// Try to ping a few times with increasing backoff in case we cannot establish
	// a connection on first boot.
	for i := 1; i <= maxRetries; i++ {
		pingCtx, cancel := context.WithTimeout(ctx, 3*time.Second)
		err = pool.Ping(pingCtx)
		cancel()

		if err == nil {
			log.Info("successfully connected to supabase", "attempt", i)
			break
		}

		log.Warn("supabase ping failed on startup, retrying...", "attempt", i, "err", err)

		if i == maxRetries {
			pool.Close()
			return nil, fmt.Errorf("ping failed after %d attempts: %w", maxRetries, err)
		}

		select {
		case <-ctx.Done():
			pool.Close()
			return nil, ctx.Err()
		case <-time.After(backoff):
			backoff *= 2
		}
	}

	return &SupabaseSink{
		pool:    pool,
		queries: New(pool),
		log:     log,
	}, nil
}

// Append writes one event safely via checked sqlc
func (s *SupabaseSink) Append(ctx context.Context, e TelemetryEvent) error {
	err := s.queries.AppendEvent(ctx, AppendEventParams{
		At:       e.At,
		Source:   e.Source,
		Kind:     e.Kind,
		Severity: e.Severity,
		Payload:  e.Payload,
	})
	if err != nil {
		return fmt.Errorf("append event: %w", err)
	}
	return nil
}

// AppendBatch leverages PostgreSQL's high-speed COPY binary protocol
func (s *SupabaseSink) AppendBatch(ctx context.Context, events []TelemetryEvent) error {
	if len(events) == 0 {
		return nil
	}

	params := make([]AppendEventBatchParams, len(events))
	for i, e := range events {
		params[i] = AppendEventBatchParams{
			At:       e.At,
			Source:   e.Source,
			Kind:     e.Kind,
			Severity: e.Severity,
			Payload:  e.Payload,
		}
	}

	// Send items in a fast COPY streaming payload
	_, err := s.queries.AppendEventBatch(ctx, params)
	return err
}

func (s *SupabaseSink) Close() { s.pool.Close() }

// WebhookSink forwards significant events to a central EWS authority.
type WebhookSink struct {
	client *http.Client
	log    *slog.Logger
}

func NewWebhookSink(log *slog.Logger) *WebhookSink {
	return &WebhookSink{
		client: &http.Client{Timeout: 10 * time.Second},
		log:    log,
	}
}

// Forward POSTs the event to the target's webhook URL if the event meets the
// target's minimum severity. Signed with the target's token so the authority
// can authenticate us. This is a best effort, it is not an error if this fails
// as the local system takes precedence.
func (w *WebhookSink) Forward(ctx context.Context, t store.EgressTarget, e TelemetryEvent) error {

	// Check that we are above the target severity.
	if e.Severity < int32(t.MinSeverity) {
		return nil
	}

	body, err := json.Marshal(e)
	if err != nil {
		return err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, t.WebhookUrl, bytes.NewReader(body))
	if err != nil {
		return err
	}

	req.Header.Set("Content-Type", "application/json")
	if t.AuthToken != "" {
		// Bearer token identifies us to the authority.
		req.Header.Set("Authorization", "Bearer "+t.AuthToken)
	}

	resp, err := w.client.Do(req)
	if err != nil {
		return fmt.Errorf("forward: %w", err)
	}

	defer resp.Body.Close()
	if resp.StatusCode >= 300 {
		return fmt.Errorf("authority returned %d", resp.StatusCode)
	}
	return nil
}
