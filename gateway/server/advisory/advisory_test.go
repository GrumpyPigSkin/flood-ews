package advisory

import (
	"testing"
	"time"
)

// TestDisposition_Valid tests that only known Dispositions are marked as valid.
func TestDisposition_Valid(t *testing.T) {
	tests := []struct {
		name string
		disp Disposition
		want bool
	}{
		{"Valid Advisory", DispositionAdvisory, true},
		{"Valid Operator Approved", DispositionOperatorApproved, true},
		{"Empty string", Disposition(""), false},
		{"Invalid arbitrary string", Disposition("malicious_command"), false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.disp.Valid(); got != tt.want {
				t.Errorf("Disposition.Valid() = %v, want %v", got, tt.want)
			}
		})
	}
}

// TestAdvisory_Stale test stale function to check if a given value is stale or not.
func TestAdvisory_Stale(t *testing.T) {
	baseTime := time.Date(2026, time.July, 10, 12, 0, 0, 0, time.UTC)
	maxAge := 5 * time.Minute

	tests := []struct {
		name       string
		observedAt time.Time
		want       bool
	}{
		{
			name:       "Zero time timestamp (fail)",
			observedAt: time.Time{},
			want:       true,
		},
		{
			name:       "Fresh observation (exactly now)",
			observedAt: baseTime,
			want:       false,
		},
		{
			name:       "Fresh observation (under max age limit)",
			observedAt: baseTime.Add(-4 * time.Minute),
			want:       false,
		},
		{
			name:       "Exact boundary limit (shouldn't be stale yet)",
			observedAt: baseTime.Add(-5 * time.Minute),
			want:       false,
		},
		{
			name:       "Stale observation (just over the limit)",
			observedAt: baseTime.Add(-5*time.Minute - time.Second),
			want:       true,
		},
		{
			name:       "Very old observation",
			observedAt: baseTime.Add(-24 * time.Hour),
			want:       true,
		},
		{
			name:       "Just a bit in the future",
			observedAt: baseTime.Add(1 * time.Minute),
			want:       false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			a := Advisory{
				ObservedAt: tt.observedAt,
			}
			if got := a.Stale(baseTime, maxAge); got != tt.want {
				t.Errorf("Advisory.Stale() = %v, want %v (ObservedAt: %v, Now: %v)", got, tt.want, tt.observedAt, baseTime)
			}
		})
	}
}
