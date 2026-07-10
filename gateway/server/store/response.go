package store

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"server/actuator"
	"server/advisory"
	"server/policy"
)

// withAuditTx runs a fn inside a transaction against the sqlc-generated
// queries, then writes a single audit log entry, then commits. If fn or the
// audit write fails, the whole transaction is rolled back.
func (s *Store) withAuditTx(ctx context.Context, actor, action, entity string, snapshot any, fn func(qtx *Queries) error) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()

	qtx := s.queries.WithTx(tx)

	if err := fn(qtx); err != nil {
		return err
	}
	if err := s.auditTx(ctx, qtx, actor, action, entity, snapshot); err != nil {
		return err
	}
	return tx.Commit()
}

// Write an entry to the audit log.
func (s *Store) auditTx(ctx context.Context, qtx *Queries, actor, action, entity string, snapshot any) error {
	detail := ""
	if snapshot != nil {
		if b, err := json.Marshal(snapshot); err == nil {
			detail = string(b)
		}
	}
	return qtx.InsertAuditLog(ctx, InsertAuditLogParams{
		At:     time.Now().UTC().Format(time.RFC3339Nano),
		Actor:  actor,
		Action: action,
		Entity: entity,
		Detail: detail,
	})
}

// Audit can only list and append.
func (s *Store) ListAudit(ctx context.Context, limit int) ([]AuditLog, error) {
	if limit <= 0 || limit > 500 {
		limit = 100
	}
	return s.queries.ListAudit(ctx, int64(limit))
}

// List sources is just a thin pass through.
func (s *Store) ListSources(ctx context.Context) ([]ExternalSource, error) {
	return s.queries.ListSources(ctx)
}

// Add/Update an ExternalSource, validate it and then add it and an audit entry.
func (s *Store) UpsertSource(ctx context.Context, e ExternalSource, actor string) error {
	if err := validateSource(e); err != nil {
		return err
	}
	return s.withAuditTx(ctx, actor, "upsert_source", e.ID, e, func(qtx *Queries) error {
		return qtx.UpsertSource(ctx, UpsertSourceParams{
			ID:          e.ID,
			Name:        e.Name,
			Enabled:     e.Enabled,
			Url:         e.Url,
			AuthHeader:  e.AuthHeader,
			AuthToken:   e.AuthToken,
			PollMs:      e.PollMs,
			Kind:        e.Kind,
			MaxAgeMs:    e.MaxAgeMs,
			MinValue:    e.MinValue,
			MaxValue:    e.MaxValue,
			Disposition: string(e.Disposition),
		})
	})
}

// Delete a source, but also log it in the audit log.
func (s *Store) DeleteSource(ctx context.Context, id, actor string) error {
	return s.withAuditTx(ctx, actor, "delete_source", id, nil, func(qtx *Queries) error {
		return qtx.DeleteSource(ctx, id)
	})
}

// List the actuators, not quite a pass through as state is stored as a JSON string.
func (s *Store) ListActuators(ctx context.Context) ([]actuator.Spec, error) {
	rows, err := s.queries.ListActuators(ctx)
	if err != nil {
		return nil, err
	}

	out := make([]actuator.Spec, len(rows))
	for i, r := range rows {
		var states []string
		if err := json.Unmarshal([]byte(r.States), &states); err != nil {
			return nil, fmt.Errorf("actuator %s: corrupt states JSON: %w", r.ID, err)
		}

		out[i] = actuator.Spec{
			ID:            r.ID,
			Name:          r.Name,
			Enabled:       r.Enabled,
			States:        states,
			FailsafeState: r.FailsafeState,
		}
	}
	return out, nil
}

// Add/Update an Actuator, record the action to the audit log.
func (s *Store) UpsertActuator(ctx context.Context, a actuator.Spec, actor string) error {
	if err := validateActuator(a); err != nil {
		return err
	}

	statesJSON, err := json.Marshal(a.States)
	if err != nil {
		return fmt.Errorf("actuator %s: encoding states: %w", a.ID, err)
	}

	return s.withAuditTx(ctx, actor, "upsert_actuator", a.ID, a, func(qtx *Queries) error {
		return qtx.UpsertActuator(ctx, UpsertActuatorParams{
			ID:            a.ID,
			Name:          a.Name,
			Enabled:       a.Enabled,
			States:        string(statesJSON),
			FailsafeState: a.FailsafeState,
		})
	})
}

// Delete an Actuator, record the action to the audit log.
func (s *Store) DeleteActuator(ctx context.Context, id, actor string) error {
	return s.withAuditTx(ctx, actor, "delete_actuator", id, nil, func(qtx *Queries) error {
		return qtx.DeleteActuator(ctx, id)
	})
}

// List rules.
func (s *Store) ListRules(ctx context.Context) ([]policy.Rule, error) {
	rows, err := s.queries.ListRules(ctx)
	if err != nil {
		return nil, err
	}

	out := make([]policy.Rule, len(rows))
	for i, r := range rows {
		out[i] = policy.Rule{
			ID:               r.ID,
			Name:             r.Name,
			Enabled:          r.Enabled,
			MatchKind:        r.MatchKind,
			MatchMinSeverity: advisory.Severity(r.MatchMinSev),
			MatchSourceID:    r.MatchSourceID,
			ActuatorID:       r.ActuatorID,
			TargetState:      r.TargetState,
			RequireOperator:  r.RequireOperator,
			Priority:         int(r.Priority),
		}
	}
	return out, nil
}

// Add/Update a rule.
func (s *Store) UpsertRule(ctx context.Context, r policy.Rule, actor string) error {
	if err := validateRule(r); err != nil {
		return err
	}
	return s.withAuditTx(ctx, actor, "upsert_rule", r.ID, r, func(qtx *Queries) error {
		return qtx.UpsertRule(ctx, UpsertRuleParams{
			ID:              r.ID,
			Name:            r.Name,
			Enabled:         r.Enabled,
			MatchKind:       r.MatchKind,
			MatchMinSev:     int64(r.MatchMinSeverity),
			MatchSourceID:   r.MatchSourceID,
			ActuatorID:      r.ActuatorID,
			TargetState:     r.TargetState,
			RequireOperator: r.RequireOperator,
			Priority:        int64(r.Priority),
		})
	})
}

// Delete a rule.
func (s *Store) DeleteRule(ctx context.Context, id, actor string) error {
	return s.withAuditTx(ctx, actor, "delete_rule", id, nil, func(qtx *Queries) error {
		return qtx.DeleteRule(ctx, id)
	})
}

// Target access List, Update and Delete

// List targets.
func (s *Store) ListTargets(ctx context.Context) ([]EgressTarget, error) {
	return s.queries.ListTargets(ctx)
}

// Add/Update a target.
func (s *Store) UpsertTarget(ctx context.Context, t EgressTarget, actor string) error {
	if err := validateTarget(t); err != nil {
		return err
	}
	return s.withAuditTx(ctx, actor, "upsert_target", t.ID, t, func(qtx *Queries) error {
		return qtx.UpsertTarget(ctx, UpsertTargetParams{
			ID:          t.ID,
			Name:        t.Name,
			Enabled:     t.Enabled,
			Type:        string(t.Type),
			Dsn:         t.Dsn,
			WebhookUrl:  t.WebhookUrl,
			AuthToken:   t.AuthToken,
			MinSeverity: int64(t.MinSeverity),
		})
	})
}

// Delete a target.
func (s *Store) DeleteTarget(ctx context.Context, id, actor string) error {
	return s.withAuditTx(ctx, actor, "delete_target", id, nil, func(qtx *Queries) error {
		return qtx.DeleteTarget(ctx, id)
	})
}

// RecordControlAction writes an audit entry for a manual operator command or
// an approved pending action.
func (s *Store) RecordControlAction(ctx context.Context, actor, actuatorID, targetState, detail string) error {
	return s.withAuditTx(ctx, actor, "control:"+targetState, actuatorID, detail, func(qtx *Queries) error {
		return nil
	})
}
