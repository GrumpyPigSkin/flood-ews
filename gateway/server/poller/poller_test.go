package poller

import (
	"context"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"

	"server/advisory"
	"server/store"
)

// MockSink implements advisory.Sink
type MockSink struct {
	mu        sync.Mutex
	Submitted []advisory.Advisory
}

func (m *MockSink) SubmitAdvisory(adv advisory.Advisory) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.Submitted = append(m.Submitted, adv)
	return nil
}

// MockOperatorQueue implements advisory.OperatorQueue
type MockOperatorQueue struct {
	mu       sync.Mutex
	Enqueued []advisory.Advisory
}

func (m *MockOperatorQueue) Enqueue(adv advisory.Advisory) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.Enqueued = append(m.Enqueued, adv)
	return nil
}

func (m *MockOperatorQueue) HasPendingFor(actuatorID, targetState string) (bool, error) {
	return false, nil
}

func TestPoller_PollOnce_SuccessAndRouting(t *testing.T) {
	// Spin up a mock local HTTP server to return the JSON payload
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{
			"severity": "warning",
			"value": 42.5,
			"unit": "mm",
			"observed_at": "2026-07-05T14:03:00Z",
			"kind": "river_level"
		}`))
	}))
	defer server.Close()

	// Set up mocks and poller
	sink := &MockSink{}
	// Quiet logger for tests
	logger := slog.New(slog.NewTextHandler(io.Discard, nil))

	p := New(sink, logger)

	src := store.ExternalSource{
		ID:      "source-1",
		Enabled: true,
		Url:     server.URL,
		Kind:    "generic",
		PollMs:  1000,
		// Configure the FieldMap so genericValidator knows how to map the JSON payload
		FieldMap: `{
			"value_path": "value",
			"unit_path": "unit",
			"observed_at_path": "observed_at",
			"kind_path": "kind",
			"severity_path": "severity"
		}`,
	}

	// Test Direct Submission Route
	p.pollOnce(context.Background(), src)

	if len(sink.Submitted) != 1 {
		t.Fatalf("expected 1 submitted advisory, got %d", len(sink.Submitted))
	}
	if sink.Submitted[0].Value != 42.5 {
		t.Errorf("expected value 42.5, got %f", sink.Submitted[0].Value)
	}
}

func TestPoller_PollOnce_BoundsChecking(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"value": 500.0, "severity": "info"}`))
	}))
	defer server.Close()

	sink := &MockSink{}
	p := New(sink, slog.New(slog.NewTextHandler(io.Discard, nil)))

	src := store.ExternalSource{
		ID:       "source-bounds",
		Url:      server.URL,
		Kind:     "generic",
		MinValue: 0,
		MaxValue: 100, // Value of 500 should trigger a drop
	}

	p.pollOnce(context.Background(), src)

	if len(sink.Submitted) != 0 {
		t.Errorf("expected advisory to be dropped due to bounds, but it was submitted")
	}
}

func TestPoller_Sync(t *testing.T) {
	sink := &MockSink{}
	p := New(sink, slog.New(slog.NewTextHandler(io.Discard, nil)))

	sources := []store.ExternalSource{
		{ID: "src-a", Enabled: true, PollMs: 5000},
		{ID: "src-b", Enabled: false, PollMs: 5000}, // Disabled, shouldn't start
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Initial Sync
	p.Sync(ctx, sources)

	p.mu.Lock()
	_, runningA := p.running["src-a"]
	_, runningB := p.running["src-b"]
	p.mu.Unlock()

	if !runningA {
		t.Error("expected src-a to be running")
	}

	if runningB {
		t.Error("expected src-b to not be running")
	}

	// Remove src-a and see if sync terminates it
	p.Sync(ctx, []store.ExternalSource{})

	p.mu.Lock()
	_, runningAAfter := p.running["src-a"]
	p.mu.Unlock()

	if runningAAfter {
		t.Error("expected src-a to be stopped after removal sync")
	}
}
