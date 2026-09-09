// Package Policy is the decision layer between advisories and actuation.
//
// It answers the question of "given this advisory what action should I take?"
// External inputs are evaluated against the policies against the operator
// defined rules. This makes sure external inputs can't take any action that
// operators do no explicitly state.

package policy

import (
	"cmp"
	"context"
	"log/slog"
	"server/actuator"
	"server/advisory"
	"slices"
	"time"
)

type Rule struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	Enabled bool   `json:"enabled"`

	// Match criteria an advisory must satisfy all set fields.
	MatchKind        string            `json:"match_kind"`
	MatchMinSeverity advisory.Severity `json:"match_min_severity"`
	MatchSourceID    string            `json:"match_source_id"`

	// Action to take when matched.
	ActuatorID  string `json:"actuator_id"`
	TargetState string `json:"target_state"`

	// RequireOperator: if true, a match does NOT act autonomously. It is
	// queued for a human to approve on the local dashboard.
	Disposition advisory.Disposition `json:"disposition"`

	// Priority: higher wins when multiple rules target the same actuator in
	// one evaluation.
	Priority int `json:"priority"`
}

// Check if an advisory matches the rule r
func (r Rule) matches(a advisory.Advisory) bool {

	if !r.Enabled {
		return false
	}

	if r.MatchKind != "" && r.MatchKind != a.Kind {
		return false
	}

	if a.Severity < r.MatchMinSeverity {
		return false
	}

	if r.MatchSourceID != "" && r.MatchSourceID != a.SourceID {
		return false
	}

	return true
}

// Interface type so we aren't coupled to the actuator daemon.
type ActuatorExecutor interface {
	Execute(ctx context.Context, cmd actuator.Command) (actuator.Result, error)
}

// Engine evaluates advisories against rules and drives the actuator daemon
// or the operator queue.
type Engine struct {
	daemon  ActuatorExecutor
	opQueue advisory.OperatorQueue
	log     *slog.Logger
	rules   []Rule // Sorted by priority descending at load.
}

// Factory function for Engine.
func NewEngine(
	d ActuatorExecutor,
	opQueue advisory.OperatorQueue,
	log *slog.Logger,
) *Engine {
	return &Engine{daemon: d, opQueue: opQueue, log: log}
}

// SetRules replaces the rule set.
func (e *Engine) SetRules(rules []Rule) {
	sorted := slices.Clone(rules)
	// Sort by priority descending
	slices.SortFunc(sorted, func(a, b Rule) int {
		return cmp.Compare(b.Priority, a.Priority)
	})

	e.rules = sorted
}

// SubmitAdvisory implements advisory.Sink: the poller hands
// each validated advisory here. We evaluate rules and either act, queue for
// operator, or do nothing.
func (e *Engine) SubmitAdvisory(a advisory.Advisory) error {
	ctx := context.Background()
	e.evaluate(ctx, a)
	return nil
}

func (e *Engine) evaluate(ctx context.Context, a advisory.Advisory) {
	acted := map[string]bool{}

	for _, r := range e.rules {
		// Check we have the rule.
		if !r.matches(a) {
			continue
		}

		// Check that this hasn't already been acted on.
		if acted[r.ActuatorID] {
			continue // a higher-priority rule already handled this actuator
		}

		// Disposition from the source can force operator approval regardless
		// of the rule.
		requireOperator :=
			r.Disposition == advisory.DispositionOperatorApproved

		if requireOperator {
			// Attach the intended action so the dashboard can show the required
			// approval.
			pending := a

			action := advisory.IntendedAction{
				ActuatorId:  r.ActuatorID,
				TargetState: r.TargetState,
				RuleId:      r.ID,
			}

			pending.IntendedAction = &action

			if err := e.opQueue.Enqueue(pending); err != nil {
				e.log.Error("policy: operator enqueue failed", "rule", r.ID, "err", err)
				continue
			}

			e.log.Info("policy: action queued for operator", "rule", r.ID,
				"actuator", r.ActuatorID, "target", r.TargetState, "source", a.SourceID)
			acted[r.ActuatorID] = true
			continue
		}

		// Autonomous action.
		cmd := actuator.Command{
			ActuatorID:    r.ActuatorID,
			TargetState:   r.TargetState,
			Reason:        "policy:" + r.ID,
			CorrelationID: a.SourceID + "@" + a.ReceivedAt.Format(time.RFC3339),
			At:            time.Now().UTC(),
		}

		if _, err := e.daemon.Execute(ctx, cmd); err != nil {
			e.log.Error("policy: actuation failed", "rule", r.ID, "err", err)
			continue
		}

		acted[r.ActuatorID] = true
	}
}

// ApprovePending is called by the /v1/control handler when an operator
// approves a queued action. It executes the intended action directly.
func (e *Engine) ApprovePending(
	ctx context.Context,
	actuatorID, targetState, reason, corr string,
) (actuator.Result, error) {
	return e.daemon.Execute(ctx, actuator.Command{
		ActuatorID:    actuatorID,
		TargetState:   targetState,
		Reason:        reason,
		CorrelationID: corr,
		At:            time.Now().UTC(),
	})
}
