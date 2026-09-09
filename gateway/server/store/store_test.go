package store

import (
	"context"
	"encoding/json"
	"testing"
)

// setupTestStore initializes a fresh, in-memory SQLite store for isolation between tests
func setupTestStore(t *testing.T) *Store {
	ctx := context.Background()
	// Using :memory: ensures a pristine database state for every single test run
	s, err := Open(ctx, ":memory:")
	if err != nil {
		t.Fatalf("failed to open test store: %v", err)
	}
	return s
}

func TestUpsertAndListSources(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()

	ctx := context.Background()
	actor := "test-operator-123"

	src := ExternalSource{
		ID:         "ex1",
		Name:       "Ex1",
		Enabled:    true,
		Url:        "https://api.weather.gov/stations/RIV01",
		AuthHeader: "Auth",
		AuthToken:  "secret-token-xyz",
		PollMs:     5000,
		Kind:       "river_level",
		MaxAgeMs:   90000,
		MinValue:   0.0,
		MaxValue:   12.5,
	}

	// Test successful Upsert
	err := s.UpsertSource(ctx, src, actor)
	if err != nil {
		t.Fatalf("UpsertSource failed: %v", err)
	}

	// Test ListSources matches what we inserted
	sources, err := s.ListSources(ctx)
	if err != nil {
		t.Fatalf("ListSources failed: %v", err)
	}

	if len(sources) != 1 {
		t.Fatalf("expected 1 source, got %d", len(sources))
	}

	got := sources[0]
	if got.ID != src.ID || got.Enabled != src.Enabled || got.Url != src.Url {
		t.Errorf("retrieved source properties mismatch. Got ID: %s, Enabled: %t", got.ID, got.Enabled)
	}

	// Verify the Audit Log was automatically written in the same transaction
	audits, err := s.ListAudit(ctx, 10)
	if err != nil {
		t.Fatalf("ListAudit failed: %v", err)
	}

	if len(audits) != 1 {
		t.Fatalf("expected 1 audit entry, got %d", len(audits))
	}

	audit := audits[0]
	if audit.Actor != actor || audit.Action != "upsert_source" || audit.Entity != src.ID {
		t.Errorf("audit log metadata mismatch: %+v", audit)
	}

	// Verify the snapshot details inside the audit log match the original object
	var snapshot ExternalSource
	if err := json.Unmarshal([]byte(audit.Detail), &snapshot); err != nil {
		t.Fatalf("failed to unmarshal audit detail payload: %v", err)
	}

	if snapshot.ID != src.ID || snapshot.Name != src.Name {
		t.Errorf("audit log JSON payload mismatch. Got ID: %s", snapshot.ID)
	}
}

func TestUpsertSourceValidationError(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()

	ctx := context.Background()

	// Invalid payload: URL is empty, and PollInterval is under 1 second constraint
	invalidSrc := ExternalSource{
		ID:     "bad-source",
		Url:    "",
		PollMs: 100,
	}

	err := s.UpsertSource(ctx, invalidSrc, "malicious-actor")
	if err == nil {
		t.Fatal("expected validation error for invalid source, but got nil")
	}

	// Verify nothing was saved to the database due to validation trigger
	sources, _ := s.ListSources(ctx)
	if len(sources) != 0 {
		t.Errorf("expected 0 sources in DB after validation failure, found %d", len(sources))
	}

	// Verify no audit log was generated for the blocked attempt
	audits, _ := s.ListAudit(ctx, 10)
	if len(audits) != 0 {
		t.Errorf("expected 0 audit entries for failed execution, found %d", len(audits))
	}
}

func TestDeleteSourceAndAuditChain(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()

	ctx := context.Background()
	actor := "admin-user"
	id := "temporary-gauge"

	// Add some data first
	src := ExternalSource{
		ID:     id,
		Url:    "https://test.com",
		PollMs: 10000,
	}
	_ = s.UpsertSource(ctx, src, actor)

	// Execute Delete operation
	err := s.DeleteSource(ctx, id, actor)
	if err != nil {
		t.Fatalf("DeleteSource failed: %v", err)
	}

	// Confirm source is gone
	sources, _ := s.ListSources(ctx)
	if len(sources) != 0 {
		t.Errorf("expected source to be deleted, but found %d entries", len(sources))
	}

	// Confirm audit log shows both the upsert AND the deletion in sequence
	audits, _ := s.ListAudit(ctx, 10)
	if len(audits) != 2 {
		t.Fatalf("expected 2 total audit logs (upsert + delete), got %d", len(audits))
	}

	// The newest audit log entry appears first due to ORDER BY id DESC
	if audits[0].Action != "delete_source" || audits[0].Entity != id {
		t.Errorf("unexpected target metadata for latest audit log entry: %s", audits[0].Action)
	}
}
