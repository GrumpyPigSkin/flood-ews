package api

import (
	"bytes"
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"server/store"
	"testing"
)

// Tests for live store

func TestLiveStore_UpdateAndSnapshot(t *testing.T) {
	ls := NewLiveStore()

	// Verify empty state
	if len(ls.snapshot()) != 0 {
		t.Errorf("expected empty snapshot, got %d items", len(ls.snapshot()))
	}

	// Insert station data
	station1 := "eui-001"
	reading1 := map[string]any{"temperature": 22.5}
	ls.UpdateStation(station1, reading1)

	station2 := "eui-002"
	reading2 := map[string]any{"temperature": 19.0}
	ls.UpdateStation(station2, reading2)

	// Verify snapshot contents
	snap := ls.snapshot()
	if len(snap) != 2 {
		t.Fatalf("expected snapshot size 2, got %d", len(snap))
	}

	// Verify individual updates overwrite existing entries
	updatedReading1 := map[string]any{"temperature": 23.0}
	ls.UpdateStation(station1, updatedReading1)

	snap = ls.snapshot()
	var foundUpdated bool
	for _, r := range snap {
		if r["temperature"] == 23.0 {
			foundUpdated = true
		}
	}

	if !foundUpdated {
		t.Error("expected to find updated reading in snapshot")
	}
}

// HTTP handler tests

func TestRoute_Healthz(t *testing.T) {
	srv := NewServer(nil, nil, nil, nil, nil, nil, "secret", "admin", false)
	router := srv.Routes()

	req := httptest.NewRequest("GET", "/healthz", nil)
	rec := httptest.NewRecorder()

	router.ServeHTTP(rec, req)

	// Assert Status Code
	if rec.Code != http.StatusOK {
		t.Errorf("expected status 200, got %d", rec.Code)
	}

	// Assert Content Type
	if contentType := rec.Header().Get("Content-Type"); contentType != "application/json" {
		t.Errorf("expected application/json, got %s", contentType)
	}

	// Assert Body
	var body map[string]string
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("failed to decode JSON response: %v", err)
	}

	if body["status"] != "ok" {
		t.Errorf("expected status 'ok', got '%s'", body["status"])
	}
}

func TestRoute_DashboardReadings(t *testing.T) {
	liveStore := NewLiveStore()
	liveStore.UpdateStation("station-123", map[string]any{"test": "test"})

	srv := NewServer(nil, liveStore, nil, nil, nil, nil, "secret", "admin", false)
	router := srv.Routes()

	req := httptest.NewRequest("GET", "/v1/dashboard/readings", nil)
	rec := httptest.NewRecorder()

	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status 200, got %d", rec.Code)
	}

	var resp []map[string]any
	if err := json.Unmarshal(rec.Body.Bytes(), &resp); err != nil {
		t.Fatalf("failed to decode response body: %v", err)
	}

	if len(resp) != 1 {
		t.Fatalf("expected 1 reading item, got %d", len(resp))
	}

	if resp[0]["test"] != "test" {
		t.Errorf("expected battery status 'test', got %v", resp[0]["test"])
	}
}

func TestLocalRoutesIsolation(t *testing.T) {
	srvRemote := NewServer(nil, nil, nil, nil, nil, nil, "secret", "password", false)
	routerRemote := srvRemote.Routes()

	req := httptest.NewRequest("GET", "/v1/config/sources", nil)
	recRemote := httptest.NewRecorder()
	routerRemote.ServeHTTP(recRemote, req)

	if recRemote.Code != http.StatusNotFound {
		t.Errorf("expected 404 for config route when isLocal=false, got %d", recRemote.Code)
	}
}

// JWT Authentication & Integration Tests

func setupTestStore(t *testing.T) *store.Store {
	ctx := context.Background()
	// Using :memory: ensures a pristine database state for every single test run
	s, err := store.Open(ctx, ":memory:")
	if err != nil {
		t.Fatalf("failed to open test store: %v", err)
	}
	return s
}

func TestLocalLogin_And_JWT_Authorization(t *testing.T) {
	// Set our expected environment variable for password validation
	const testPass = "super-secret-password"
	const hashedPass = "$2a$12$Ym5CyE6PY8tzI6IRDlbo6ONFRuDiqN98.XSrKSF5dbKnf8fdhUgcK"

	jwtSecret := "my-jwt-test-key"

	// Create a real (but silent) logger so s.log doesn't panic
	testLogger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{
		Level: slog.LevelError + 1,
	}))

	// Setup mock stores.
	mockStore := setupTestStore(t)
	liveStore := NewLiveStore()

	// Pass initialized dependencies instead of nil
	srv := NewServer(mockStore, liveStore, nil, nil, nil, testLogger, jwtSecret, hashedPass, true)
	router := srv.Routes()

	// Attempt login with a bad password
	badBody, _ := json.Marshal(loginRequest{Password: "wrong-pass"})
	reqLoginBad := httptest.NewRequest("POST", "/v1/auth/login", bytes.NewReader(badBody)) // Fixed path
	recLoginBad := httptest.NewRecorder()
	router.ServeHTTP(recLoginBad, reqLoginBad)

	if recLoginBad.Code != http.StatusUnauthorized {
		t.Errorf("expected unauthorized 401, got %d", recLoginBad.Code)
	}

	// Attempt login with correct password
	goodBody, _ := json.Marshal(loginRequest{Password: testPass})
	reqLoginGood := httptest.NewRequest("POST", "/v1/auth/login", bytes.NewReader(goodBody)) // Fixed path
	recLoginGood := httptest.NewRecorder()
	router.ServeHTTP(recLoginGood, reqLoginGood)

	if recLoginGood.Code != http.StatusOK {
		t.Fatalf("expected login ok 200, got %d", recLoginGood.Code)
	}

	var loginResp map[string]string
	_ = json.Unmarshal(recLoginGood.Body.Bytes(), &loginResp)
	token := loginResp["token"]
	if token == "" {
		t.Fatal("expected token string inside response, got empty")
	}

	// Test requesting a protected route without a valid token should return 401
	reqUnauth := httptest.NewRequest("GET", "/v1/config/sources", nil)
	recUnauth := httptest.NewRecorder()
	router.ServeHTTP(recUnauth, reqUnauth)

	if recUnauth.Code != http.StatusUnauthorized {
		t.Errorf("expected unauthorized 401 for unauthenticated request, got %d", recUnauth.Code)
	}

	// Test requesting a protected route with the valid generated JWT
	reqAuth := httptest.NewRequest("GET", "/v1/config/sources", nil)
	reqAuth.Header.Set("Authorization", "Bearer "+token)
	recAuth := httptest.NewRecorder()
	router.ServeHTTP(recAuth, reqAuth)

	// Since mockStore is empty but not nil, this will safely execute instead of panic-dereferencing
	if recAuth.Code != http.StatusOK {
		t.Errorf("expected status 200, got %d", recAuth.Code)
	}
}

// Helper function tests.

func TestActorFrom_HeaderFallback(t *testing.T) {
	reqWithHeader := httptest.NewRequest("GET", "/", nil)
	reqWithHeader.Header.Set("X-Actor", "admin-user")

	sLocal := NewServer(nil, nil, nil, nil, nil, nil, "secret", "password", true)

	if actor := sLocal.actorFrom(reqWithHeader); actor != "admin-user" {
		t.Errorf("expected 'admin-user', got '%s'", actor)
	}

	sRemote := NewServer(nil, nil, nil, nil, nil, nil, "secret", "password", false)
	if actor := sRemote.actorFrom(reqWithHeader); actor != "local" {
		t.Errorf("expected 'local' fallback under non-local server environment, got '%s'", actor)
	}

	reqWithoutHeader := httptest.NewRequest("GET", "/", nil)
	if actor := sLocal.actorFrom(reqWithoutHeader); actor != "local" {
		t.Errorf("expected 'local', got '%s'", actor)
	}
}

func TestWriteErr(t *testing.T) {
	rec := httptest.NewRecorder()
	customErr := json.Unmarshal([]byte("{invalid json"), &json.SyntaxError{})

	testLogger := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{
		Level: slog.LevelError + 1,
	}))

	s := NewServer(nil, nil, nil, nil, nil, testLogger, "secret", "password", false)
	s.writeErr(rec, http.StatusBadRequest, customErr)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("expected status 400, got %d", rec.Code)
	}

	var errResp map[string]string
	_ = json.Unmarshal(rec.Body.Bytes(), &errResp)

	if _, hasErrorKey := errResp["error"]; !hasErrorKey {
		t.Error("expected JSON payload to contain an 'error' key")
	}
}
