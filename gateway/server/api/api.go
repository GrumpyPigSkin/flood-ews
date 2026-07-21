// Package API serves the gateways HTTP service.
//
// /healthz           unauthenticated  liveness / failover probe
// /v1/dashboard/...  read             live sensor state, recent, history-proxy
// /v1/config/...     write (LOCAL)    the single config-authority write path
// /v1/control/...    write (LOCAL)    Local control confirmation and override

package api

import (
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"server/actuator"
	"server/policy"
	"server/poller"
	"server/store"
	"sync"
	"time"

	"github.com/go-chi/jwtauth/v5"
	"golang.org/x/crypto/bcrypt"
)

// LiveStore holds the latest per-station state for dashboard reads.
type LiveStore struct {
	mu       sync.RWMutex
	stations map[string]map[string]any // deviceEUI -> latest reading object
}

// Factory function.
func NewLiveStore() *LiveStore {
	return &LiveStore{stations: map[string]map[string]any{}}
}

// Struct to handle login requests .
type loginRequest struct {
	Password string `json:"password"`
}

// UpdateStation is called from the uplink path to publish the latest reading
// for a station.
func (l *LiveStore) UpdateStation(eui string, reading map[string]any) {
	l.mu.Lock()
	l.stations[eui] = reading
	l.mu.Unlock()
}

// Server wires handlers to their dependencies.
type Server struct {
	store         *store.Store
	live          *LiveStore
	poll          *poller.Poller
	daemon        *actuator.Daemon
	engine        *policy.Engine
	log           *slog.Logger
	tokenAuth     *jwtauth.JWTAuth
	adminPassword string

	// isLocal gates whether the config-write and control tiers are mounted.
	// True only for the trusted-network deployment.
	isLocal bool
}

// Factory function.
func NewServer(
	st *store.Store,
	live *LiveStore,
	p *poller.Poller,
	d *actuator.Daemon,
	e *policy.Engine,
	log *slog.Logger,
	jwtSecret string,
	adminPassword string,
	isLocal bool,
) *Server {

	// Use HS256 symmetric key, since only local dashboard can access protected
	// endpoints.
	tokenAuth := jwtauth.New("HS256", []byte(jwtSecret), nil)

	return &Server{
		store:         st,
		live:          live,
		poll:          p,
		daemon:        d,
		engine:        e,
		log:           log,
		tokenAuth:     tokenAuth,
		adminPassword: adminPassword,
		isLocal:       isLocal,
	}
}

func (s *Server) Routes() http.Handler {
	mux := http.NewServeMux()

	// Health status.
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	})

	// Dashboard
	dash := http.NewServeMux()
	dash.HandleFunc("GET /v1/dashboard/readings", s.handleReadings)
	dash.HandleFunc("GET /v1/dashboard/actuators", s.handleActuatorStates)
	mux.Handle("/v1/dashboard/", dash)

	// Everything else is local only.
	if s.isLocal {
		// Publicly expose the login endpoint directly on the main router on the Pi only.
		mux.HandleFunc("POST /v1/auth/login", s.handleLocalLogin)

		// Strictly require verified JWTs and check permissions.
		cfg := http.NewServeMux()

		// Reusable middleware stacks using jwtauth
		jwtAuth := jwtauth.Verifier(s.tokenAuth)
		jwtRequired := jwtauth.Authenticator(s.tokenAuth)
		writeScope := s.requireScope("config:write")

		// Read endpoints: authenticated only
		cfg.Handle("GET /v1/config/sources", jwtAuth(jwtRequired(http.HandlerFunc(s.handleListSources))))
		cfg.Handle("GET /v1/config/targets", jwtAuth(jwtRequired(http.HandlerFunc(s.handleListTargets))))
		cfg.Handle("GET /v1/config/actuators", jwtAuth(jwtRequired(http.HandlerFunc(s.handleListActuators))))
		cfg.Handle("GET /v1/config/rules", jwtAuth(jwtRequired(http.HandlerFunc(s.handleListRules))))
		cfg.Handle("GET /v1/config/audit", jwtAuth(jwtRequired(http.HandlerFunc(s.handleAudit))))

		// Write/Modify endpoints: authenticated AND require "config:write" scope
		cfg.Handle("PUT /v1/config/sources", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleUpsertSource)))))
		cfg.Handle("DELETE /v1/config/sources/{id}", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleDeleteSource)))))
		cfg.Handle("PUT /v1/config/targets", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleUpsertTarget)))))
		cfg.Handle("PUT /v1/config/actuators", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleUpsertActuator)))))
		cfg.Handle("DELETE /v1/config/actuators/{id}", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleDeleteActuator)))))
		cfg.Handle("PUT /v1/config/rules", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleUpsertRule)))))
		cfg.Handle("DELETE /v1/config/rules/{id}", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleDeleteRule)))))
		mux.Handle("/v1/config/", cfg)

		ctl := http.NewServeMux()
		ctl.Handle("POST /v1/control/actuate", jwtAuth(jwtRequired(writeScope(http.HandlerFunc(s.handleActuate)))))
		mux.Handle("/v1/control/", ctl)
	}

	return mux
}

// Check the password the client sent us against the stored admin password.
func checkPassword(hashedAdminPassword, passwordFromClient string) bool {

	if hashedAdminPassword == "" {
		return false
	}

	err := bcrypt.CompareHashAndPassword([]byte(hashedAdminPassword), []byte(passwordFromClient))
	return err == nil
}

// Handle a login request, can only login locally.
func (s *Server) handleLocalLogin(w http.ResponseWriter, r *http.Request) {

	var req loginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	// Check the incoming password against the stored password.
	// We expect the client to hash the plain text first as SHA256.
	if !checkPassword(s.adminPassword, req.Password) {
		s.writeErr(w, http.StatusUnauthorized, nil)
		return
	}

	// Generate a local token containing the "config:write" scope
	_, tokenString, err := s.tokenAuth.Encode(map[string]any{
		"sub":   "local_admin",
		"scope": "config:write",                        // Gives access to write paths
		"exp":   time.Now().Add(24 * time.Hour).Unix(), // Valid for 24 hours
	})

	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	writeJSON(w, http.StatusOK, map[string]string{"token": tokenString})
}

// requireScope is a middleware that enforces that the authenticated user's JWT
// contains the specified required scope in its claims.
func (s *Server) requireScope(requiredScope string) func(http.Handler) http.Handler {

	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			_, claims, err := jwtauth.FromContext(r.Context())
			if err != nil {
				s.writeErr(w, http.StatusForbidden, err)
				return
			}

			scope, _ := claims["scope"].(string)
			if scope != requiredScope {
				writeJSON(w, http.StatusForbidden, map[string]string{"error": "forbidden: insufficient scope"})
				return
			}

			next.ServeHTTP(w, r)
		})
	}
}

// Snapshot and send the readings.
func (s *Server) handleReadings(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, s.live.snapshot())
}

// Config handlers

func (s *Server) handleListSources(w http.ResponseWriter, r *http.Request) {
	sources, err := s.store.ListSources(r.Context())
	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, sources)
}

func (s *Server) handleUpsertSource(w http.ResponseWriter, r *http.Request) {

	var src store.ExternalSource
	if err := json.NewDecoder(r.Body).Decode(&src); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}
	actor := s.actorFrom(r)
	if err := s.store.UpsertSource(r.Context(), src, actor); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	// Resync the sources asynchronously.
	bgCtx := context.WithoutCancel(r.Context())
	go s.resyncPoller(bgCtx)
	writeJSON(w, http.StatusOK, map[string]string{"status": "saved", "id": src.ID})
}

func (s *Server) handleDeleteSource(w http.ResponseWriter, r *http.Request) {

	id := r.PathValue("id")
	if err := s.store.DeleteSource(r.Context(), id, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	// Resync the sources asynchronously.
	bgCtx := context.WithoutCancel(r.Context())
	go s.resyncPoller(bgCtx)
	writeJSON(w, http.StatusOK, map[string]string{"status": "deleted", "id": id})
}

func (s *Server) handleListTargets(w http.ResponseWriter, r *http.Request) {
	targets, err := s.store.ListTargets(r.Context())
	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, targets)
}

func (s *Server) handleUpsertTarget(w http.ResponseWriter, r *http.Request) {

	var t store.EgressTarget
	if err := json.NewDecoder(r.Body).Decode(&t); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	if err := s.store.UpsertTarget(r.Context(), t, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	writeJSON(w, http.StatusOK, map[string]string{"status": "saved", "id": t.ID})
}

func (s *Server) handleAudit(w http.ResponseWriter, r *http.Request) {

	entries, err := s.store.ListAudit(r.Context(), 100)
	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	writeJSON(w, http.StatusOK, entries)
}

// Actuator config handlers

func (s *Server) handleListActuators(w http.ResponseWriter, r *http.Request) {

	specs, err := s.store.ListActuators(r.Context())
	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	writeJSON(w, http.StatusOK, specs)
}

func (s *Server) handleUpsertActuator(w http.ResponseWriter, r *http.Request) {

	var a actuator.Spec
	if err := json.NewDecoder(r.Body).Decode(&a); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	if err := s.store.UpsertActuator(r.Context(), a, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	s.reloadActuators(r.Context())
	writeJSON(w, http.StatusOK, map[string]string{"status": "saved", "id": a.ID})
}

func (s *Server) handleDeleteActuator(w http.ResponseWriter, r *http.Request) {

	id := r.PathValue("id")
	if err := s.store.DeleteActuator(r.Context(), id, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	s.reloadActuators(r.Context())
	writeJSON(w, http.StatusOK, map[string]string{"status": "deleted", "id": id})
}

// Policy rule config handlers.

func (s *Server) handleListRules(w http.ResponseWriter, r *http.Request) {

	rules, err := s.store.ListRules(r.Context())
	if err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	writeJSON(w, http.StatusOK, rules)
}

func (s *Server) handleUpsertRule(w http.ResponseWriter, r *http.Request) {

	var rule policy.Rule
	if err := json.NewDecoder(r.Body).Decode(&rule); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	if err := s.store.UpsertRule(r.Context(), rule, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	s.reloadRules(r.Context())
	writeJSON(w, http.StatusOK, map[string]string{"status": "saved", "id": rule.ID})
}

func (s *Server) handleDeleteRule(w http.ResponseWriter, r *http.Request) {

	id := r.PathValue("id")
	if err := s.store.DeleteRule(r.Context(), id, s.actorFrom(r)); err != nil {
		s.writeErr(w, http.StatusInternalServerError, err)
		return
	}

	s.reloadRules(r.Context())
	writeJSON(w, http.StatusOK, map[string]string{"status": "deleted", "id": id})
}

// Dashboard actuator states:
func (s *Server) handleActuatorStates(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, s.daemon.States())
}

// Manual actuation

type actuateRequest struct {
	ActuatorID  string `json:"actuator_id"`
	TargetState string `json:"target_state"`
	Reason      string `json:"reason"`
}

// handleActuate is the operator override: directly command an actuator,
// bypassing policy.
func (s *Server) handleActuate(w http.ResponseWriter, r *http.Request) {

	var req actuateRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	actor := s.actorFrom(r)
	reason := req.Reason
	if reason == "" {
		reason = "manual:" + actor
	}

	res, err := s.engine.ApprovePending(r.Context(), req.ActuatorID, req.TargetState, reason,
		"manual@"+time.Now().UTC().Format(time.RFC3339))
	if err != nil {
		s.writeErr(w, http.StatusBadRequest, err)
		return
	}

	// Audit the operator action explicitly.
	_ = s.store.RecordControlAction(r.Context(), actor, req.ActuatorID, req.TargetState, reason)
	writeJSON(w, http.StatusOK, map[string]any{
		"status": "actuated", "actuator": res.ActuatorID,
		"from": res.PriorState, "to": res.NewState, "applied": res.Applied,
	})
}

// Reload helpers.

// Reload actuator list.
func (s *Server) reloadActuators(ctx context.Context) {

	specs, err := s.store.ListActuators(ctx)
	if err != nil {
		s.log.Error("reload actuators failed", "err", err)
		return
	}

	s.daemon.SetSpecs(ctx, specs)
}

// Reload rules list.
func (s *Server) reloadRules(ctx context.Context) {

	rules, err := s.store.ListRules(ctx)
	if err != nil {
		s.log.Error("reload rules failed", "err", err)
		return
	}

	s.engine.SetRules(rules)
}

// resyncPoller reloads sources from the store and reconciles the poller.
func (s *Server) resyncPoller(ctx context.Context) {

	sources, err := s.store.ListSources(ctx)
	if err != nil {
		s.log.Error("resync: list sources failed", "err", err)
		return
	}

	s.poll.Sync(ctx, sources)
}

// actorFrom extracts the operator identity from the request.
func (s *Server) actorFrom(r *http.Request) string {
	// Only trust the debugging header if running on the local trusted network
	if s.isLocal {
		if v := r.Header.Get("X-Actor"); v != "" {
			return v
		}
	}
	return "local"
}

func (s *Server) writeErr(w http.ResponseWriter, code int, err error) {
	// Log the actual error internally guard against nil logger
	if s.log != nil {
		s.log.Error("API error handler", "status", code, "err", err)
	}

	// Return a generic error to the client if it's an internal server failure
	msg := ""
	if err != nil {
		msg = err.Error()
	}

	if code >= 500 {
		msg = "An internal server error occurred"
	}

	writeJSON(w, code, map[string]string{"error": msg})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

// Get a snapshot of the stored readings for each station.
func (l *LiveStore) snapshot() []map[string]any {
	l.mu.RLock()
	defer l.mu.RUnlock()
	out := make([]map[string]any, 0, len(l.stations))
	for _, r := range l.stations {
		cloned := make(map[string]any, len(r))
		for k, v := range r {
			cloned[k] = v
		}
		out = append(out, cloned)
	}
	return out
}
