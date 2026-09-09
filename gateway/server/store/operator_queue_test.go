package store

import (
	"context"
	"errors"
	"server/advisory"
	"testing"
	"time"
)

func makeAdv(sourceID string, disposition advisory.Disposition) advisory.Advisory {
	now := time.Now().UTC()
	return advisory.Advisory{
		SourceID:   sourceID,
		Kind:       "river_level",
		Severity:   advisory.SeverityWarning,
		Value:      4.2,
		Unit:       "m",
		ObservedAt: now,
		ReceivedAt: now,
	}
}

// TestEnqueueWithIntendedAction covers the policy.Engine.evaluate() path, it
// uses DispositionAdvisory for coverage although we shouldn't expect those to
// be routed here for operator approval.
func TestEnqueueWithIntendedAction(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()
	ctx := context.Background()

	a := makeAdv("ex1", advisory.DispositionAdvisory)

	action := advisory.IntendedAction{
		ActuatorId:  "gate-1",
		TargetState: "open",
		RuleId:      "rule-42",
	}

	a.IntendedAction = &action

	if err := s.Enqueue(a); err != nil {
		t.Fatalf("Enqueue failed: %v", err)
	}

	pending, err := s.ListPending(ctx)
	if err != nil {
		t.Fatalf("ListPending failed: %v", err)
	}

	if len(pending) != 1 {
		t.Fatalf("expected 1 pending item, got %d", len(pending))
	}

	item := pending[0]
	if item.ActuatorID != "gate-1" || item.TargetState != "open" || item.RuleID != "rule-42" {
		t.Fatalf("Intended action not round-tripped correctly: %+v", item)
	}

	if item.Status != "pending" {
		t.Fatalf("Expected status pending, got %q", item.Status)
	}
}

// Test the enqueue path without an intended action, this should just come back
// as empty fields rather than an error.
func TestEnqueueWithoutIntendedAction(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()
	ctx := context.Background()

	if err := s.Enqueue(makeAdv("ex2", advisory.DispositionOperatorApproved)); err != nil {
		t.Fatalf("Enqueue failed: %v", err)
	}

	pending, err := s.ListPending(ctx)
	if err != nil {
		t.Fatalf("ListPending failed: %v", err)
	}

	if len(pending) != 1 {
		t.Fatalf("expected 1 pending item, got %d", len(pending))
	}

	if pending[0].ActuatorID != "" || pending[0].TargetState != "" {
		t.Fatalf("expected no intended action, got %+v", pending[0])
	}
}

// Test approving a pending queue item, try it twice to check we get the ErrorAlreadyResolved error.
func TestResolvePendingApproved(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()
	ctx := context.Background()

	if err := s.Enqueue(makeAdv("ex3", advisory.DispositionOperatorApproved)); err != nil {
		t.Fatalf("Enqueue failed: %v", err)
	}

	pending, err := s.ListPending(ctx)
	if err != nil || len(pending) != 1 {
		t.Fatalf("expected 1 pending item, got %d, err %v", len(pending), err)
	}
	id := pending[0].ID

	resolved, err := s.ResolvePending(ctx, id, QueueItemApproved, "operator-1")
	if err != nil {
		t.Fatalf("ResolvePending failed: %v", err)
	}

	if resolved.Status != QueueItemApproved || resolved.ResolvedBy != "operator-1" {
		t.Fatalf("unexpected resolved item: %+v", resolved)
	}

	pending, err = s.ListPending(ctx)
	if err != nil {
		t.Fatalf("ListPending failed: %v", err)
	}
	if len(pending) != 0 {
		t.Fatalf("expected 0 pending items after approval, got %d", len(pending))
	}

	// Resolving again should fail as it is no longer pending.
	if _, err := s.ResolvePending(ctx, id, QueueItemRejected, "operator-2"); !errors.Is(err, ErrorAlreadyResolved) {
		t.Fatalf("expected ErrorAlreadyResolved, got %v", err)
	}
}

// Try and resolve a non-existent queue item, should be rejected.
func TestResolvePendingNotFound(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()
	ctx := context.Background()

	if _, err := s.ResolvePending(ctx, 999, QueueItemApproved, "operator-1"); !errors.Is(err, ErrorPendingNotFound) {
		t.Fatalf("expected ErrorPendingNotFound, got %v", err)
	}
}

// Test rejecting a queued item.
func TestResolvePendingReject(t *testing.T) {
	s := setupTestStore(t)
	defer s.Close()
	ctx := context.Background()

	a := makeAdv("ex4", advisory.DispositionAdvisory)
	a.IntendedAction = &advisory.IntendedAction{
		ActuatorId:  "gate-2",
		TargetState: "closed",
		RuleId:      "rule-7",
	}
	if err := s.Enqueue(a); err != nil {
		t.Fatalf("Enqueue failed: %v", err)
	}

	pending, err := s.ListPending(ctx)
	if err != nil || len(pending) != 1 {
		t.Fatalf("expected 1 pending item, got %d, err %v", len(pending), err)
	}

	resolved, err := s.ResolvePending(ctx, pending[0].ID, QueueItemRejected, "operator-1")
	if err != nil {
		t.Fatalf("ResolvePending failed: %v", err)
	}
	if resolved.Status != QueueItemRejected {
		t.Fatalf("expected status rejected, got %q", resolved.Status)
	}

	pending, err = s.ListPending(ctx)
	if err != nil {
		t.Fatalf("ListPending failed: %v", err)
	}
	if len(pending) != 0 {
		t.Fatalf("expected 0 pending items after rejection, got %d", len(pending))
	}
}
