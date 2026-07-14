package poller

import (
	"encoding/json"
	"fmt"
	"math"
	"server/advisory"
	"server/store"
	"time"

	"github.com/tidwall/gjson"
)

// To create a validator that goes from some external source returned value, to
// our internal advisory type we need to know from the operator the format of
// the data and how it maps onto us. So, on the dashboard when adding a new
// external source they also provide JSON like so:
//
// {
//   "value_path": "level_mm",
//   "severity_thresholds": [
//     { "min": 0,    "severity": "info" },
//     { "min": 2000, "severity": "watch" },
//     { "min": 4000, "severity": "warning" },
//     { "min": 8000, "severity": "critical" }
//   ]
// }

// FieldMapping tells the generic validator where inside an arbitrary JSON
// response body to find each piece of advisory data. Every path is a
// dot-separated route through the decoded JSON, with optional [index] for
// arrays, e.g. "data.readings[0].level_mm".
type FieldMapping struct {
	ValuePath      string `json:"value_path"`
	UnitPath       string `json:"unit_path,omitempty"`
	ObservedAtPath string `json:"observed_at_path,omitempty"`
	KindPath       string `json:"kind_path,omitempty"`

	// Only one of these two is normally set. SeverityPath wins if both are
	// present and the lookup succeeds.
	SeverityPath string            `json:"severity_path,omitempty"`
	SeverityMap  map[string]string `json:"severity_map,omitempty"`

	// SeverityThresholds buckets adv.Value when the source gives a raw number
	// with no severity field of its own. The highest Min the value clears wins,
	// so entries don't need to be pre-sorted.
	SeverityThresholds []SeverityThreshold `json:"severity_thresholds,omitempty"`
}

type SeverityThreshold struct {
	Min      float64 `json:"min"`
	Severity string  `json:"severity"` // "info" | "watch" | "warning" | "critical"
}

// Validate the incoming JSON payload and extract the values into FieldMapping
// Then use these values to build a valid advisory.Advisory
func genericValidator(src store.ExternalSource, body []byte, now time.Time) (advisory.Advisory, error) {

	// Parse the configuration field mapping
	var fm FieldMapping
	if src.FieldMap != "" {
		if err := json.Unmarshal([]byte(src.FieldMap), &fm); err != nil {
			return advisory.Advisory{}, fmt.Errorf("source %s: bad field_map: %w", src.ID, err)
		}
	}

	// Extract the primary value directly.
	valResult := gjson.GetBytes(body, fm.ValuePath)
	if !valResult.Exists() {
		return advisory.Advisory{}, fmt.Errorf("source %s: value_path %q not found", src.ID, fm.ValuePath)
	}
	value := valResult.Float()

	// Guard against NaN
	if math.IsNaN(value) {
		return advisory.Advisory{}, fmt.Errorf("source %s: value is NaN", src.ID)
	}

	adv := advisory.Advisory{
		SourceID:   src.ID,
		Value:      value,
		Kind:       getStringWithFallback(body, fm.KindPath, src.Kind),
		Unit:       getStringWithFallback(body, fm.UnitPath, ""),
		ReceivedAt: now,
	}

	// Determine Severity
	var severitySet bool
	if fm.SeverityPath != "" {
		sevResult := gjson.GetBytes(body, fm.SeverityPath)
		if sevResult.Exists() {
			adv.Severity = severityFromMapped(sevResult.String(), fm.SeverityMap)
			severitySet = true
		}
	}

	if !severitySet {
		if sev, ok := severityFromThresholds(value, fm.SeverityThresholds); ok {
			adv.Severity = sev
		} else {
			adv.Severity = advisory.SeverityUnknown
		}
	}

	// Handle timestamp
	if fm.ObservedAtPath != "" {
		tsResult := gjson.GetBytes(body, fm.ObservedAtPath)
		if tsResult.Exists() {
			t, err := parseTimestamp(tsResult.String())
			if err != nil {
				// Return the error.
				return advisory.Advisory{}, fmt.Errorf("source %s: invalid observed_at timestamp %q: %w", src.ID, tsResult.String(), err)
			}
			adv.ObservedAt = t.UTC()
		}
	}

	return adv, nil
}

// Helper to parse multiple date/time standards safely
func parseTimestamp(raw string) (time.Time, error) {

	layouts := []string{
		time.RFC3339,
		time.RFC3339Nano,
		"2006-01-02T15:04:05",
	}

	for _, layout := range layouts {
		if t, err := time.Parse(layout, raw); err == nil {
			return t, nil
		}
	}

	return time.Time{}, fmt.Errorf("unsupported format")
}

// Get a string from JSON, if it can't be found, return the fallback.
func getStringWithFallback(body []byte, path string, fallback string) string {
	if path == "" {
		return fallback
	}
	res := gjson.GetBytes(body, path)
	if !res.Exists() || res.String() == "" {
		return fallback
	}
	return res.String()
}

// Map the thresholds given to a severity.
func severityFromThresholds(value float64, thresholds []SeverityThreshold) (advisory.Severity, bool) {
	found := false
	bestMin := math.Inf(-1)
	var bestSev string
	for _, t := range thresholds {
		if value >= t.Min && t.Min >= bestMin {
			bestMin, bestSev, found = t.Min, t.Severity, true
		}
	}
	if !found {
		return advisory.SeverityUnknown, false
	}
	return severityFromStr(bestSev), true
}

// Map a string to a severity.
func severityFromMapped(val string, m map[string]string) advisory.Severity {
	if mapped, ok := m[val]; ok {
		return severityFromStr(mapped)
	}
	return severityFromStr(val)
}

func severityFromStr(s string) advisory.Severity {
	switch s {
	case "info":
		return advisory.SeverityInfo
	case "watch":
		return advisory.SeverityWatch
	case "warning":
		return advisory.SeverityWarning
	case "critical":
		return advisory.SeverityCritical
	default:
		return advisory.SeverityUnknown
	}
}
