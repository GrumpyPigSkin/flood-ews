package poller

import (
	"context"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"server/actuator"
	"server/advisory"
	"server/policy"
	"server/store"
	"testing"
)

// Real data from government website to test the validators correctly fetch the data from the JSON.
const TestJSONRaise = `{
  "items" : [ {
    "@id" : "http://environment.data.gov.uk/flood-monitoring/data/readings/680403-level-stage-i-15_min-m/2026-08-11T06-45-00Z" ,
    "dateTime" : "2026-08-11T06:45:00Z" ,
    "measure" : "http://environment.data.gov.uk/flood-monitoring/id/measures/680403-level-stage-i-15_min-m" ,
    "value" : 2
  }
   ]
}`

const TestJSONLower = `{
  "items" : [ {
    "@id" : "http://environment.data.gov.uk/flood-monitoring/data/readings/680403-level-stage-i-15_min-m/2026-08-11T06-45-00Z" ,
    "dateTime" : "2026-08-11T06:45:00Z" ,
    "measure" : "http://environment.data.gov.uk/flood-monitoring/id/measures/680403-level-stage-i-15_min-m" ,
    "value" : 0.134
  }
   ]
}`

func TestExternalSource_DrivesActuatorViaPolicy(t *testing.T) {

	logger := slog.New(slog.NewTextHandler(io.Discard, nil))

	daemon := actuator.NewDaemon(actuator.NewSimBackend(logger), 0, logger)

	// Setup actuator
	daemon.SetSpecs(context.Background(), []actuator.Spec{
		{
			ID:            "barrier-1",
			Name:          "Test Barrier",
			States:        []string{"RAISED", "LOWERED"},
			FailsafeState: "LOWERED",
			Enabled:       true,
		},
	})

	// Setup policies.
	engine := policy.NewEngine(daemon, &MockOperatorQueue{}, logger)
	engine.SetRules([]policy.Rule{
		{
			ID: "barrier-raise", Enabled: true, Priority: 10,
			MatchKind: "river_level", MatchMinSeverity: advisory.SeverityCritical,
			ActuatorID: "barrier-1", TargetState: "RAISED",
		},
		{
			ID: "barrier-lower", Enabled: true, Priority: 5,
			MatchKind: "river_level", MatchMinSeverity: advisory.SeverityInfo,
			ActuatorID: "barrier-1", TargetState: "LOWERED",
		}})

	// Start dummy servers to fetch the fake API data.
	criticalSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte(TestJSONRaise))
	}))
	defer criticalSrv.Close()

	clearSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte(TestJSONLower))
	}))
	defer clearSrv.Close()

	// The mock external source should match the example JSON above.
	src := store.ExternalSource{
		ID:      "ea-gauge-1",
		Enabled: true,
		Kind:    "river_level",
		FieldMap: `{
			"value_path": "items.0.value",
			"observed_at_path": "items.0.dateTime",
			"severity_thresholds": [
				{ "min": 0,    "severity": "info" },
				{ "min": 0.55, "severity": "watch" },
				{ "min": 1.0,  "severity": "warning" },
				{ "min": 1.8,  "severity": "critical" }
			]
		}`,
	}

	p := New(engine, logger)

	if got := daemon.States()["barrier-1"]; got != "LOWERED" {
		t.Fatalf("expected initial state 'LOWERED', got %q", got)
	}

	// The barrier should raise.
	src.Url = criticalSrv.URL
	p.pollOnce(context.Background(), src)
	if got := daemon.States()["barrier-1"]; got != "RAISED" {
		t.Fatalf("expected barrier RAISED after critical reading, got %q", got)
	}

	// Poll the clear reading, the barrier should lower again.
	src.Url = clearSrv.URL
	p.pollOnce(context.Background(), src)
	if got := daemon.States()["barrier-1"]; got != "LOWERED" {
		t.Fatalf("expected barrier LOWERED after clear reading, got %q", got)
	}

}
