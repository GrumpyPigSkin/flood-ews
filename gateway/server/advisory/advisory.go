// Package advisory defines the external-input types.
//
// The design principle is that external data is advisory and not a command. We
// poll data from an external source i.e. a neighbouring authority and treat it
// as another input. We can use this information to warn an operator and they
// can then take action whether or not they deem it necessary. Or we can use it
// along side values read by us to advise an action.

package advisory

import (
	"time"
)

// Disposition is the per-source policy for how a validated external alert is
// allowed to affect the system.
type Disposition string

const (
	// DispositionAdvisory: the alert is evaluated by the local policy engine and
	// MAY drive an autonomous action if a rule permits. Even so, a rule can
	// require operator approval, and no single external claim actuates anything
	// the policy does not explicitly allow.
	DispositionAdvisory Disposition = "advisory"

	// DispositionOperatorApproved: the alert is always queued for an operator at
	// the local dashboard to confirm before it influences anything, regardless of
	// what any rule says. Used for sources trusted less, or actions weightier.
	DispositionOperatorApproved Disposition = "operator_approved"
)

// Is the deposition d a valid value.
func (d Disposition) Valid() bool {
	return d == DispositionAdvisory || d == DispositionOperatorApproved
}

// Severity is a normalised level, mapped from each source's own scheme by
// its validator so the policy engine sees consistent levels.
type Severity int

const (
	SeverityUnknown Severity = iota
	SeverityInfo
	SeverityWatch
	SeverityWarning
	SeverityCritical
)

// Intended action contains the information to trigger an actuator response for
// an advisory.
type IntendedAction struct {
	ActuatorId  string
	TargetState string
	RuleId      string
}

type Advisory struct {
	// The external source this came from.
	SourceID string

	// Kind is a short tag, e.g. "river_level", so we can match rules against it.
	Kind string

	// The severity tag.
	Severity Severity

	// Value + Unit carry the measurement when the advisory is a
	// reading (e.g. 4200, "mm"). Zero Value is valid for pure alerts.
	Value float64
	Unit  string

	// ObservedAt is the source's timestamp for the observation.
	ObservedAt time.Time

	// The time we pulled the advisory.
	ReceivedAt time.Time

	// Raw is the original decoded payload, retained for debugging.
	IntendedAction *IntendedAction
}

// Stale reports whether the observation is older than maxAge relative to
// now.
//
// A trusted source returning a stale "all clear" is itself a hazard,
// so the ingest path drops or flags stale advisories rather than trusting
// them.
func (a Advisory) Stale(now time.Time, maxAge time.Duration) bool {
	if a.ObservedAt.IsZero() {
		// No timestamp so cannot trust.
		return true
	}
	return now.Sub(a.ObservedAt) > maxAge
}

// The poller calls this to deliver a validated advisory into the local response
// pipeline. The policy engine implements this interface.
type Sink interface {
	// SubmitAdvisory delivers a validated advisory for local evaluation.
	SubmitAdvisory(a Advisory) error
}

// OperatorQueue receives advisories that require operator approval before they
// influence the system.
type OperatorQueue interface {

	// Enqueue an advisory to the operator queue
	Enqueue(a Advisory) error

	// Does a pending item already exist for this actuator and state
	HasPendingFor(actuatorID, targetState string) (bool, error)
}
