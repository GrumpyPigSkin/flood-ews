// Package actuator drives physical or simulated responses from commands.
//
// An actuator is anything with a discrete set of states, it could be a flood
// barrier, an alarm, or some other notifier. The daemon does not care what the
// device is, it drives a named actuator to a named target state via a pluggable
// backend.
//
// The daemon also provides some basic safety:
// - failsafe: Each actuator declares the state it assumes on fault, watchdog
// timeout or shutdown.
// - watchdog: if the control loop stops feeding the daemon a heartbeat, each
// actuator is driven to it's failsafe state.
// - idempotent: commanding the current state is a no-op.
// - validated: commands for unknown actuators or states are rejected.

package actuator

import (
	"context"
	"fmt"
	"log/slog"
	"slices"
	"sync"
	"time"
)

// Backend performs the actual state change on a device. A real actuator backend
// would implement the same interface.
type Backend interface {

	// Apply drives the device to state. Must be safe to call repeatedly with the
	// same state. Returns an error if the device cannot be driven.
	Apply(ctx context.Context, ActuatorId, state string) error
}

// Spec is the configuration of one actuator persisted in SQLite.
type Spec struct {
	ID            string   `json:"id"`
	Name          string   `json:"name"`
	States        []string `json:"states"`         // allowed states, e.g. ["open","closed"]
	FailsafeState string   `json:"failsafe_state"` // must be one of States
	Enabled       bool     `json:"enabled"`
}

// Ensure that the state given in state is a valid state for Spec.
func (s Spec) validState(state string) bool {
	return slices.Contains(s.States, state)
}

// Command is an instruction to drive one actuator to a target state. Carries
// provenance so every action is traceable.
type Command struct {
	ActuatorID    string
	TargetState   string
	Reason        string // Human readable.
	CorrelationID string // Ties back to the advisory/operator action
	At            time.Time
}

// Result records the outcome of a command.
type Result struct {
	Command
	PriorState string
	NewState   string
	Applied    bool // false if it was a no-op
	Err        string
}

// Daemon owns the actuator state and enforces safety. All state transitions go
// through it, so it is the single point that knows every actuator's current
// state and can drive everything to failsafe on trouble.
type Daemon struct {
	backend Backend
	log     *slog.Logger

	mu    sync.Mutex
	specs map[string]Spec
	state map[string]string // Actuator ID -> current state.

	// Watchdog.
	wdTimeout time.Duration
	lastBeat  time.Time
	onResult  func(Result) // Optional sink.
}

// Factory function for Daemon.
func NewDaemon(backend Backend, wdTimeout time.Duration, log *slog.Logger) *Daemon {
	return &Daemon{
		backend:   backend,
		log:       log,
		specs:     map[string]Spec{},
		state:     map[string]string{},
		wdTimeout: wdTimeout,
		lastBeat:  time.Now(),
	}
}

// OnResult registers a callback invoked after every command.
func (d *Daemon) OnResult(fn func(Result)) { d.onResult = fn }

// SetSpecs loads/updates the actuator set from config and drives any newly-
// added actuator to its failsafe state as a known-safe starting point.
// Called at startup and on config change.
func (d *Daemon) SetSpecs(ctx context.Context, specs []Spec) {

	d.mu.Lock()
	defer d.mu.Unlock()

	next := map[string]Spec{}
	for _, s := range specs {
		if s.Enabled {
			next[s.ID] = s
		}
	}

	// Initialise new actuators to failsafe.
	for id, s := range next {
		if _, known := d.state[id]; !known {
			if err := d.applyLocked(ctx, s, s.FailsafeState, "init:failsafe", ""); err != nil {
				d.log.Error("actuator init failed", "id", id, "err", err)
			}
		}
	}

	// Forget actuators removed from config.
	for id := range d.state {
		if _, ok := next[id]; !ok {
			delete(d.state, id)
		}
	}
	d.specs = next
}

// Heartbeat must be called periodically by the control loop. Missing it for
// longer than wdTimeout trips the watchdog.
func (d *Daemon) Heartbeat() {
	d.mu.Lock()
	d.lastBeat = time.Now()
	d.mu.Unlock()
}

func (d *Daemon) Execute(ctx context.Context, c Command) (Result, error) {

	d.mu.Lock()
	defer d.mu.Unlock()

	// Check the actuator is valid
	spec, ok := d.specs[c.ActuatorID]
	if !ok {
		return Result{Command: c, Err: "unknown actuator"},
			fmt.Errorf("unknown actuator %q", c.ActuatorID)
	}

	// Check the new command state is a valid state for the actuator.
	if !spec.validState(c.TargetState) {
		return Result{Command: c, Err: "invalid state"},
			fmt.Errorf("actuator %q: invalid state %q", c.ActuatorID, c.TargetState)
	}

	// Check that the state has changed, if it hasn't we don't need to do anything.
	prior := d.state[c.ActuatorID]
	if prior == c.TargetState {
		res := Result{Command: c, PriorState: prior, NewState: prior, Applied: false}
		d.emit(res)
		return res, nil
	}

	// Apply the command and check for an error.
	if err := d.backend.Apply(ctx, c.ActuatorID, c.TargetState); err != nil {
		res := Result{Command: c, PriorState: prior, NewState: prior, Applied: false, Err: err.Error()}
		d.emit(res)
		return res, err
	}

	// No error, log the fact we have successfully applied the command.
	d.state[c.ActuatorID] = c.TargetState
	res := Result{Command: c, PriorState: prior, NewState: c.TargetState, Applied: true}
	d.log.Info("actuator commanded", "id", c.ActuatorID, "from", prior,
		"to", c.TargetState, "reason", c.Reason, "corr", c.CorrelationID)
	d.emit(res)
	return res, nil

}

// applyLocked drives an actuator to a state without the caller/idempotency
// checks, should be called under a lock.
func (d *Daemon) applyLocked(ctx context.Context, spec Spec, state string, reason, corr string) any {
	if err := d.backend.Apply(ctx, spec.ID, state); err != nil {
		return err
	}
	prior := d.state[spec.ID]
	d.state[spec.ID] = state
	d.emit(Result{
		Command:    Command{ActuatorID: spec.ID, TargetState: state, Reason: reason, CorrelationID: corr, At: time.Now().UTC()},
		PriorState: prior, NewState: state, Applied: true,
	})
	return nil
}

// DriveAllFailsafe forces every actuator to its declared failsafe state.
// Invoked by the watchdog and on shutdown.
func (d *Daemon) DriveAllFailsafe(ctx context.Context, reason string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	for id, spec := range d.specs {
		if d.state[id] == spec.FailsafeState {
			continue
		}
		if err := d.applyLocked(ctx, spec, spec.FailsafeState, reason, ""); err != nil {
			d.log.Error("failsafe drive failed", "id", id, "err", err)
		} else {
			d.log.Warn("actuator driven to failsafe", "id", id, "state", spec.FailsafeState, "reason", reason)
		}
	}
}

// RunWatchdog blocks until ctx is cancelled, checking the heartbeat. If the
// control loop goes silent past wdTimeout, everything is driven to failsafe.
// On ctx cancel (shutdown) it also drives failsafe, then returns.
func (d *Daemon) RunWatchdog(ctx context.Context) {
	if d.wdTimeout <= 0 {
		<-ctx.Done()
		d.DriveAllFailsafe(context.Background(), "shutdown")
	}

	ticker := time.NewTicker(d.wdTimeout / 2)
	defer ticker.Stop()
	tripped := false

	for {
		select {
		case <-ctx.Done():
			d.DriveAllFailsafe(context.Background(), "shutdown")
			return
		case <-ticker.C:
			d.mu.Lock()
			silent := time.Since(d.lastBeat)
			d.mu.Unlock()
			if silent > d.wdTimeout {
				if !tripped {
					d.log.Error("watchdog tripped: control loop silent", "silent", silent)
					d.DriveAllFailsafe(ctx, "watchdog")
					tripped = true
				}
			} else {
				tripped = false
			}
		}
	}
}

// States returns a snapshot of current actuator states.
func (d *Daemon) States() map[string]string {
	d.mu.Lock()
	defer d.mu.Unlock()
	out := make(map[string]string, len(d.state))
	for k, v := range d.state {
		out[k] = v
	}
	return out
}

// Check on result is bound and call it.
func (d *Daemon) emit(r Result) {
	if d.onResult != nil {
		d.onResult(r)
	}
}

// Get the current state of the given actuator.
func (d *Daemon) CurrentState(actuatorId string) (string, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	state, ok := d.state[actuatorId]
	if !ok {
		return "",
			fmt.Errorf("unknown actuator %q", actuatorId)
	}

	return state, nil
}

// For testing in the final-project building a proper actuator is out of scope.
// So to test that some alert results in an actuation, I create a simulated
// actuator that just logs the command.
type SimBackend struct {
	log *slog.Logger
}

// Factory function
func NewSimBackend(log *slog.Logger) *SimBackend { return &SimBackend{log: log} }

// Apply function just logs the fact it has been called.
func (s *SimBackend) Apply(_ context.Context, actuatorID, state string) error {
	s.log.Info("SIM actuator apply", "id", actuatorID, "state", state)
	return nil
}
