package actuator

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"sync"
	"testing"
	"time"
)

// MockBackend tracks applied states and lets us simulate errors.
type MockBackend struct {
	mu           sync.Mutex
	Applied      map[string][]string // ActuatorID -> list of states applied
	FailOnDevice string              // If set, match ActuatorID to return an error
}

func NewMockBackend() *MockBackend {
	return &MockBackend{
		Applied: make(map[string][]string),
	}
}

// Mock apply function.
func (m *MockBackend) Apply(ctx context.Context, actuatorID, state string) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.FailOnDevice == actuatorID {
		return errors.New("hardware failure")
	}

	m.Applied[actuatorID] = append(m.Applied[actuatorID], state)
	return nil
}

// Helper to create a discard logger so tests don't flood the terminal.
func newDiscardLogger() *slog.Logger {
	return slog.New(slog.NewTextHandler(io.Discard, nil))
}

// TestSetSpecs verifies that new specs force initialization to failsafe,
// and removed specs are correctly cleaned up.
func TestSetSpecs(t *testing.T) {
	backend := NewMockBackend()
	daemon := NewDaemon(backend, 5*time.Second, newDiscardLogger())

	specs := []Spec{
		{ID: "gate-1", Name: "River Gate", States: []string{"open", "closed"}, FailsafeState: "closed", Enabled: true},
		{ID: "gate-2", Name: "Secondary Gate", States: []string{"open", "closed"}, FailsafeState: "closed", Enabled: false}, // Disabled, should be ignored
	}

	daemon.SetSpecs(context.Background(), specs)

	states := daemon.States()
	if _, ok := states["gate-2"]; ok {
		t.Error("expected disabled actuator 'gate-2' to be ignored")
	}

	if val, ok := states["gate-1"]; !ok || val != "closed" {
		t.Errorf("expected 'gate-1' to initialize to failsafe 'closed', got %q", val)
	}

	// Verify update logic, Add a new one, remove the old one.
	nextSpecs := []Spec{
		{ID: "pump-1", Name: "Drain Pump", States: []string{"on", "off"}, FailsafeState: "off", Enabled: true},
	}
	daemon.SetSpecs(context.Background(), nextSpecs)

	states = daemon.States()
	if _, ok := states["gate-1"]; ok {
		t.Error("expected 'gate-1' to be removed from tracked states")
	}
	if val, ok := states["pump-1"]; !ok || val != "off" {
		t.Errorf("expected 'pump-1' to be initialized to 'off', got %q", val)
	}
}

// TestExecute verifies command processing.
func TestExecute(t *testing.T) {
	backend := NewMockBackend()
	daemon := NewDaemon(backend, 5*time.Second, newDiscardLogger())

	specs := []Spec{
		{ID: "gate-1", Name: "River Gate", States: []string{"open", "closed"}, FailsafeState: "closed", Enabled: true},
	}
	daemon.SetSpecs(context.Background(), specs)

	tests := []struct {
		name        string
		command     Command
		setupMock   func()
		wantApplied bool
		wantErr     bool
		wantState   string
	}{
		{
			name:        "Successful State Transition",
			command:     Command{ActuatorID: "gate-1", TargetState: "open", Reason: "rising water"},
			setupMock:   func() {},
			wantApplied: true,
			wantErr:     false,
			wantState:   "open",
		},
		{
			name:        "Idempotent Call (No-Op)",
			command:     Command{ActuatorID: "gate-1", TargetState: "open", Reason: "duplicate message"},
			setupMock:   func() {},
			wantApplied: false,
			wantErr:     false,
			wantState:   "open", // remains open
		},
		{
			name:        "Unknown Actuator ID",
			command:     Command{ActuatorID: "invalid-id", TargetState: "open"},
			setupMock:   func() {},
			wantApplied: false,
			wantErr:     true,
			wantState:   "",
		},
		{
			name:        "Invalid State Choice",
			command:     Command{ActuatorID: "gate-1", TargetState: "half-open"},
			setupMock:   func() {},
			wantApplied: false,
			wantErr:     true,
			wantState:   "open",
		},
		{
			name:    "Backend Hardware Error",
			command: Command{ActuatorID: "gate-1", TargetState: "closed"},
			setupMock: func() {
				backend.FailOnDevice = "gate-1"
			},
			wantApplied: false,
			wantErr:     true,
			wantState:   "open", // Should remain open since command failed
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tt.setupMock()
			res, err := daemon.Execute(context.Background(), tt.command)

			if (err != nil) != tt.wantErr {
				t.Fatalf("Execute() error status = %v, wantErr %v", err, tt.wantErr)
			}

			if res.Applied != tt.wantApplied {
				t.Errorf("Result.Applied = %v, want %v", res.Applied, tt.wantApplied)
			}

			if tt.wantState != "" {
				currentStates := daemon.States()
				if currentStates[tt.command.ActuatorID] != tt.wantState {
					t.Errorf("Expected current state to be %q, got %q", tt.wantState, currentStates[tt.command.ActuatorID])
				}
			}
		})
	}
}

// TestOnResult ensures our event dispatching callback works correctly.
func TestOnResult(t *testing.T) {
	backend := NewMockBackend()
	daemon := NewDaemon(backend, 5*time.Second, newDiscardLogger())
	daemon.SetSpecs(context.Background(), []Spec{
		{ID: "gate-1", States: []string{"open", "closed"}, FailsafeState: "closed", Enabled: true},
	})

	var received Result
	var mu sync.Mutex
	daemon.OnResult(func(r Result) {
		mu.Lock()
		received = r
		mu.Unlock()
	})

	cmd := Command{ActuatorID: "gate-1", TargetState: "open", Reason: "test callback"}
	_, _ = daemon.Execute(context.Background(), cmd)

	mu.Lock()
	defer mu.Unlock()
	if received.ActuatorID != "gate-1" || received.NewState != "open" {
		t.Errorf("OnResult callback did not receive expected transaction metrics. Got: %v", received)
	}
}

// TestWatchdog_Trips validates that if the heartbeat isn't updated within the
// timeout window, the background watchdog routine correctly trips into failsafe.
func TestWatchdog_Trips(t *testing.T) {
	backend := NewMockBackend()
	// Set a very tight timeout for testing speed
	wdTimeout := 40 * time.Millisecond
	daemon := NewDaemon(backend, wdTimeout, newDiscardLogger())

	daemon.SetSpecs(context.Background(), []Spec{
		{ID: "gate-1", States: []string{"open", "closed"}, FailsafeState: "closed", Enabled: true},
	})

	// Manually push it out of failsafe status so we can watch it trip back
	_, _ = daemon.Execute(context.Background(), Command{ActuatorID: "gate-1", TargetState: "open"})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Spin up the watchdog routine
	go daemon.RunWatchdog(ctx)

	// Sleep past the watchdog timeout window without hitting Heartbeat()
	time.Sleep(wdTimeout * 2)

	states := daemon.States()
	if states["gate-1"] != "closed" {
		t.Errorf("watchdog failed to trip actuator into failsafe state; current state is %q", states["gate-1"])
	}
}
