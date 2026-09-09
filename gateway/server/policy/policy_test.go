package policy

import (
	"context"
	"log/slog"
	"os"
	"server/actuator"
	"server/advisory"
	"testing"
	"time"
)

// Mocks

type mockDaemon struct {
	executed    []actuator.Command
	errToReturn error
}

func (m *mockDaemon) Execute(ctx context.Context, cmd actuator.Command) (actuator.Result, error) {
	m.executed = append(m.executed, cmd)
	return actuator.Result{}, m.errToReturn
}

type mockQueue struct {
	enqueued    []advisory.Advisory
	errToReturn error
}

func (m *mockQueue) Enqueue(a advisory.Advisory) error {
	m.enqueued = append(m.enqueued, a)
	return m.errToReturn
}

// Tests suites.
func TestEngine_SetRules(t *testing.T) {
	log := slog.New(slog.NewTextHandler(os.Stdout, nil))
	engine := NewEngine(nil, nil, log)

	rules := []Rule{
		{ID: "low-priority", Priority: 1},
		{ID: "high-priority", Priority: 10},
		{ID: "mid-priority", Priority: 5},
	}

	engine.SetRules(rules)

	if len(engine.rules) != 3 {
		t.Fatalf("expected 3 rules, got %d", len(engine.rules))
	}

	if engine.rules[0].ID != "high-priority" || engine.rules[1].ID != "mid-priority" || engine.rules[2].ID != "low-priority" {
		t.Errorf("rules not sorted by priority descending: %+v", engine.rules)
	}
}

func TestEngine_SubmitAdvisory(t *testing.T) {
	now := time.Now().UTC()
	logger := slog.New(slog.NewTextHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelError}))

	tests := []struct {
		name           string
		rules          []Rule
		advisory       advisory.Advisory
		expectActuated []string
		expectQueued   []string
	}{
		{
			name: "Autonomous trigger",
			rules: []Rule{
				{ID: "r1", Enabled: true, MatchKind: "sensor", ActuatorID: "barrier", TargetState: "CLOSE", Priority: 1},
			},
			advisory:       advisory.Advisory{Kind: "sensor", Severity: 1, SourceID: "s1", ReceivedAt: now},
			expectActuated: []string{"r1"},
		},
		{
			name: "Disabled rule ignored",
			rules: []Rule{
				{ID: "r1", Enabled: false, MatchKind: "sensor", ActuatorID: "barrier", TargetState: "CLOSE", Priority: 1},
			},
			advisory: advisory.Advisory{Kind: "sensor", Severity: 1, SourceID: "s1", ReceivedAt: now},
		},
		{
			name: "Severity below threshold",
			rules: []Rule{
				{ID: "r1", Enabled: true, MatchMinSeverity: 5, ActuatorID: "barrier", TargetState: "CLOSE"},
			},
			advisory: advisory.Advisory{Severity: 2, ReceivedAt: now},
		},
		{
			name: "Higher priority overrides lower priority",
			rules: []Rule{
				{ID: "r-low", Enabled: true, MatchKind: "sensor", ActuatorID: "barrier", TargetState: "HALF_CLOSE", Priority: 1},
				{ID: "r-high", Enabled: true, MatchKind: "sensor", ActuatorID: "barrier", TargetState: "FULL_CLOSE", Priority: 10},
			},
			advisory:       advisory.Advisory{Kind: "sensor", ReceivedAt: now},
			expectActuated: []string{"r-high"},
		},
		{
			name: "Multiple distinct actuators both trigger",
			rules: []Rule{
				{ID: "r-barrier", Enabled: true, ActuatorID: "barrier", TargetState: "CLOSE", Priority: 5},
				{ID: "r-alarm", Enabled: true, ActuatorID: "alarm", TargetState: "ON", Priority: 5},
			},
			advisory:       advisory.Advisory{ReceivedAt: now},
			expectActuated: []string{"r-barrier", "r-alarm"},
		},
		{
			name: "Dangerous actions require operator approval",
			rules: []Rule{
				{ID: "r-siren", Enabled: true, Disposition: advisory.DispositionOperatorApproved, ActuatorID: "siren", TargetState: "ON"},
			},
			advisory:     advisory.Advisory{ReceivedAt: now},
			expectQueued: []string{"r-siren"},
		},
		{
			name: "Operator approved disposition forces queueing",
			rules: []Rule{
				{ID: "r-barrier", Enabled: true, Disposition: advisory.DispositionOperatorApproved, ActuatorID: "barrier", TargetState: "CLOSE"},
			},
			advisory:     advisory.Advisory{ReceivedAt: now},
			expectQueued: []string{"r-barrier"},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			daemon := &mockDaemon{}
			queue := &mockQueue{}

			engine := NewEngine(nil, queue, logger)
			engine.daemon = daemon
			engine.SetRules(tt.rules)

			err := engine.SubmitAdvisory(tt.advisory)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			// Verify execution
			if len(daemon.executed) != len(tt.expectActuated) {
				t.Errorf("expected %d activations, got %d", len(tt.expectActuated), len(daemon.executed))
			}
			for i, ruleID := range tt.expectActuated {
				if i < len(daemon.executed) && daemon.executed[i].Reason != "policy:"+ruleID {
					t.Errorf("expected rule %s, got %s", ruleID, daemon.executed[i].Reason)
				}
			}

			// Verify operator queue
			if len(queue.enqueued) != len(tt.expectQueued) {
				t.Errorf("expected %d queued items, got %d", len(tt.expectQueued), len(queue.enqueued))
			}
			for i, ruleID := range tt.expectQueued {
				if i < len(queue.enqueued) {
					action := queue.enqueued[i].IntendedAction
					if action == nil || action.RuleId != ruleID {
						t.Errorf("expected queued action for rule %s, got %+v", ruleID, action)
					}
				}
			}
		})
	}
}

func TestEngine_ApprovePending(t *testing.T) {
	daemon := &mockDaemon{}
	logger := slog.New(slog.NewTextHandler(os.Stdout, nil))
	engine := Engine{daemon: daemon, log: logger}

	ctx := context.Background()
	_, err := engine.ApprovePending(ctx, "siren", "ON", "manual override", "corr-123")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if len(daemon.executed) != 1 {
		t.Fatalf("expected 1 execution, got %d", len(daemon.executed))
	}

	cmd := daemon.executed[0]
	if cmd.ActuatorID != "siren" || cmd.TargetState != "ON" || cmd.Reason != "manual override" || cmd.CorrelationID != "corr-123" {
		t.Errorf("command parameters passed incorrectly: %+v", cmd)
	}
}
