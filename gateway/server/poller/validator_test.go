package poller

import (
	"testing"
	"time"

	"server/advisory"
	"server/store"
)

func TestGenericValidator(t *testing.T) {
	now := time.Now().UTC()

	tests := []struct {
		name         string
		source       store.ExternalSource
		body         []byte
		expectedAdv  advisory.Advisory
		expectErr    bool
		errSubstring string
	}{
		{
			name: "Success: Basic mapping with threshold severity",
			source: store.ExternalSource{
				ID: "src-1",
				FieldMap: `{
					"value_path": "metrics.water_level",
					"unit_path": "metrics.unit",
					"kind_path": "type",
					"severity_thresholds": [
						{"min": 0, "severity": "info"},
						{"min": 100, "severity": "warning"},
						{"min": 500, "severity": "critical"}
					]
				}`,
			},
			body: []byte(`{"type": "flood", "metrics": {"water_level": 150.5, "unit": "cm"}}`),
			expectedAdv: advisory.Advisory{
				SourceID:   "src-1",
				Value:      150.5,
				Kind:       "flood",
				Unit:       "cm",
				ReceivedAt: now,
				Severity:   advisory.SeverityWarning,
			},
			expectErr: false,
		},
		{
			name: "Success: Severity mapped directly from path with mapping",
			source: store.ExternalSource{
				ID: "src-2",
				FieldMap: `{
					"value_path": "level",
					"severity_path": "status",
					"severity_map": {
						"ALERT": "critical",
						"OK": "info"
					}
				}`,
			},
			body: []byte(`{"level": 42, "status": "ALERT"}`),
			expectedAdv: advisory.Advisory{
				SourceID:   "src-2",
				Value:      42,
				ReceivedAt: now,
				Severity:   advisory.SeverityCritical,
			},
			expectErr: false,
		},
		{
			name: "Success: Fallback to source kind and default values",
			source: store.ExternalSource{
				ID:       "src-3",
				Kind:     "default_kind",
				FieldMap: `{"value_path": "val"}`,
			},
			body: []byte(`{"val": 10.0}`),
			expectedAdv: advisory.Advisory{
				SourceID:   "src-3",
				Value:      10.0,
				Kind:       "default_kind",
				ReceivedAt: now,
				Severity:   advisory.SeverityUnknown,
			},
			expectErr: false,
		},
		{
			name: "Success: Parse ObservedAt timestamp successfully",
			source: store.ExternalSource{
				ID: "src-4",
				FieldMap: `{
					"value_path": "val",
					"observed_at_path": "timestamp"
				}`,
			},
			body: []byte(`{"val": 10.0, "timestamp": "2026-07-14T15:30:00Z"}`),
			expectedAdv: advisory.Advisory{
				SourceID:   "src-4",
				Value:      10.0,
				ReceivedAt: now,
				ObservedAt: time.Date(2026, 7, 14, 15, 30, 0, 0, time.UTC),
				Severity:   advisory.SeverityUnknown,
			},
			expectErr: false,
		},
		{
			name:         "Error: Invalid JSON body",
			source:       store.ExternalSource{ID: "src-5"},
			body:         []byte(`{invalid-json`),
			expectErr:    true,
			errSubstring: "decode: invalid json body",
		},
		{
			name: "Error: Bad FieldMap JSON",
			source: store.ExternalSource{
				ID:       "src-6",
				FieldMap: `{bad_json}`,
			},
			body:         []byte(`{"val": 10}`),
			expectErr:    true,
			errSubstring: "bad field_map",
		},
		{
			name: "Error: Missing value path in payload",
			source: store.ExternalSource{
				ID:       "src-7",
				FieldMap: `{"value_path": "missing_field"}`,
			},
			body:         []byte(`{"other": 10}`),
			expectErr:    true,
			errSubstring: `value_path "missing_field" not found`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := genericValidator(tt.source, tt.body, now)
			if tt.expectErr {
				if err == nil {
					t.Fatalf("expected error containing %q, but got nil", tt.errSubstring)
				}
				return
			}

			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			// Validate core fields
			if got.SourceID != tt.expectedAdv.SourceID {
				t.Errorf("expected SourceID %q, got %q", tt.expectedAdv.SourceID, got.SourceID)
			}

			if got.Value != tt.expectedAdv.Value {
				t.Errorf("expected Value %f, got %f", tt.expectedAdv.Value, got.Value)
			}

			if got.Kind != tt.expectedAdv.Kind {
				t.Errorf("expected Kind %q, got %q", tt.expectedAdv.Kind, got.Kind)
			}

			if got.Unit != tt.expectedAdv.Unit {
				t.Errorf("expected Unit %q, got %q", tt.expectedAdv.Unit, got.Unit)
			}

			if !got.ReceivedAt.Equal(tt.expectedAdv.ReceivedAt) {
				t.Errorf("expected ReceivedAt %v, got %v", tt.expectedAdv.ReceivedAt, got.ReceivedAt)
			}

			if got.Severity != tt.expectedAdv.Severity {
				t.Errorf("expected Severity %v, got %v", tt.expectedAdv.Severity, got.Severity)
			}

			if !got.ObservedAt.Equal(tt.expectedAdv.ObservedAt) {
				t.Errorf("expected ObservedAt %v, got %v", tt.expectedAdv.ObservedAt, got.ObservedAt)
			}
		})
	}
}

func TestGetStringWithFallback(t *testing.T) {
	body := []byte(`{"exist": "hello", "empty": ""}`)

	tests := []struct {
		name     string
		path     string
		fallback string
		expected string
	}{
		{"Path empty", "", "fallback", "fallback"},
		{"Path missing", "missing", "fallback", "fallback"},
		{"Value empty string", "empty", "fallback", "fallback"},
		{"Value exists", "exist", "fallback", "hello"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {

			got := getStringWithFallback(body, tt.path, tt.fallback)

			if got != tt.expected {
				t.Errorf("expected %q, got %q", tt.expected, got)
			}
		})
	}
}

func TestSeverityFromThresholds(t *testing.T) {
	thresholds := []SeverityThreshold{
		{Min: 0, Severity: "info"},
		{Min: 100, Severity: "warning"},
		{Min: 50, Severity: "watch"}, // out of order on purpose
	}

	tests := []struct {
		name             string
		value            float64
		expectedSeverity advisory.Severity
		expectedFound    bool
	}{
		{"Below all", -10, advisory.SeverityUnknown, false},
		{"Exactly lowest", 0, advisory.SeverityInfo, true},
		{"Between info and watch", 40, advisory.SeverityInfo, true},
		{"Exactly watch", 50, advisory.SeverityWatch, true},
		{"Between watch and warning", 75, advisory.SeverityWatch, true},
		{"Exactly warning", 100, advisory.SeverityWarning, true},
		{"Well above warning", 1000, advisory.SeverityWarning, true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotSev, gotFound := severityFromThresholds(tt.value, thresholds)
			if gotFound != tt.expectedFound {
				t.Errorf("expected found=%t, got %t", tt.expectedFound, gotFound)
			}
			if gotSev != tt.expectedSeverity {
				t.Errorf("expected severity %v, got %v", tt.expectedSeverity, gotSev)
			}
		})
	}
}

func TestSeverityFromMapped(t *testing.T) {
	m := map[string]string{
		"CRIT": "critical",
		"WARN": "warning",
		"NONE": "unknown_val",
	}

	tests := []struct {
		name     string
		input    string
		expected advisory.Severity
	}{
		{"Mapped critical", "CRIT", advisory.SeverityCritical},
		{"Mapped warning", "WARN", advisory.SeverityWarning},
		{"Mapped unknown string value", "NONE", advisory.SeverityUnknown},
		{"Unmapped fallback to standard severity string", "watch", advisory.SeverityWatch},
		{"Unmapped completely invalid", "some-garbage", advisory.SeverityUnknown},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := severityFromMapped(tt.input, m)
			if got != tt.expected {
				t.Errorf("expected %v, got %v", tt.expected, got)
			}
		})
	}
}

func TestSeverityFromStr(t *testing.T) {
	tests := []struct {
		input    string
		expected advisory.Severity
	}{
		{"info", advisory.SeverityInfo},
		{"watch", advisory.SeverityWatch},
		{"warning", advisory.SeverityWarning},
		{"critical", advisory.SeverityCritical},
		{"unknown", advisory.SeverityUnknown},
		{"", advisory.SeverityUnknown},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			got := severityFromStr(tt.input)
			if got != tt.expected {
				t.Errorf("expected %v, got %v", tt.expected, got)
			}
		})
	}
}
