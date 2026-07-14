// Wire up all the components and run the server.
//
// store      SQLite config
// actuator   sim/GPIO daemon with watchdog + failsafe
// policy     advisory -> action decision engine
// poller     outbound pull of external advisories
// egress     push to Supabase read-model + EWS authority webhook
// telemetry  ChirpStack MQTT -> WebSocket bridge + uplink fan-out
// api        HTTP endpoints
//

package main

import (
	"context"
	"encoding/json"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/gorilla/websocket"

	"server/actuator"
	"server/advisory"
	"server/api"
	"server/egress"
	"server/policy"
	"server/poller"
	"server/store"
	"server/telemetry"
)

// Handle environment variables with a safe default.
func env(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	// Config store.
	st, err := store.Open(ctx, env("CONFIG_DB", "gateway.db"))
	if err != nil {
		logger.Error("open config store", "err", err)
		os.Exit(1)
	}
	defer st.Close()

	// Live sensor store (REST mirror of the WS hub).
	live := api.NewLiveStore()

	// Response side: sim backend -> actuator daemon -> policy engine.
	daemon := actuator.NewDaemon(actuator.NewSimBackend(logger), 15*time.Second, logger)
	daemon.OnResult(func(r actuator.Result) {
		if r.Applied {
			logger.Info("actuation", "actuator", r.ActuatorID, "to", r.NewState, "reason", r.Reason)
			_ = st.RecordControlAction(context.Background(), "policy", r.ActuatorID, r.NewState, r.Reason)
		}
	})
	opQueue := &stubOperatorQueue{log: logger}
	engine := policy.NewEngine(daemon, opQueue, logger)

	// Load stored data and setup policy engine.
	if specs, err := st.ListActuators(ctx); err != nil {
		logger.Error("list actuators", "err", err)
	} else {
		daemon.SetSpecs(ctx, specs)
	}

	if rules, err := st.ListRules(ctx); err != nil {
		logger.Error("list rules", "err", err)
	} else {
		engine.SetRules(rules)
	}

	// Start actuator watchdog and heartbeat.
	go daemon.RunWatchdog(ctx)
	go runHeartbeat(ctx, daemon, 5*time.Second)

	// Start poller, pull external advisories into the policy engine.
	p := poller.New(engine, opQueue, logger)
	if sources, err := st.ListSources(ctx); err != nil {
		logger.Error("list sources", "err", err)
	} else {
		p.Sync(ctx, sources)
	}
	defer p.StopAll()

	// Egress targets TODO: Finish supabase storage.
	targets, err := st.ListTargets(ctx)
	if err != nil {
		logger.Error("list targets", "err", err)
	}

	var egressSink telemetry.EgressSink
	for _, t := range targets {
		if t.Enabled && store.EgressType(t.Type) == store.TypeSupabase {
			supa, err := egress.OpenSupabase(ctx, t.Dsn, logger)
			if err != nil {
				logger.Error("open supabase", "id", t.ID, "err", err)
				continue
			}
			defer supa.Close()
			egressSink = supa // only now is the interface non-nil
			logger.Info("egress: supabase ready", "id", t.ID)
		}
	}

	// Telemetry bridge: hub + fan-out + MQTT.
	tcfg := telemetry.Config{}
	hub := telemetry.NewHub(tcfg, logger)
	bridge := telemetry.NewBridge(telemetry.BridgeDeps{
		Hub:     hub,
		Live:    live,
		Egress:  egressSink,
		Webhook: egress.NewWebhookSink(logger),
		Targets: targets,
		Log:     logger,
	})
	go bridge.RunEgressFlusher(ctx, 5*time.Second)

	mqttClient, err := bridge.ConnectMQTT(telemetry.MQTTOptions{
		Broker:   env("MQTT_BROKER", "tcp://192.168.1.109:1883"),
		Username: env("MQTT_USERNAME", ""),
		Password: env("MQTT_PASSWORD", ""),
	})

	if err != nil {
		logger.Warn("MQTT connect (non-fatal)", "err", err)
	}
	defer mqttClient.Disconnect(250)

	// HTTP: API + WebSocket + /recent
	isLocal := env("ROLE", "local") == "local"
	secretKey := env("JWT_SECRET", "super-secret-token")
	adminPassword := env("ADMIN_PASSWORD", "")

	if adminPassword == "" && isLocal {
		logger.Error("ADMIN_PASSWORD environment variable must be set in local mode")
	}

	apiSrv := api.NewServer(st, live, p, daemon, engine, logger, secretKey, adminPassword, isLocal)

	// TODO: Remove when I move backend to RPI!!!
	upgrader := websocket.Upgrader{
		CheckOrigin: func(_ *http.Request) bool { return true },
	}

	mux := http.NewServeMux()
	mux.Handle("/", apiSrv.Routes())
	mux.HandleFunc("GET /ws", func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			logger.Warn("ws upgrade", "err", err)
			return
		}
		logger.Info("ws client connected", "remote", r.RemoteAddr)
		hub.ServeWS(conn)
	})

	mux.HandleFunc("GET /recent", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(hub.Snapshot())
	})

	httpSrv := &http.Server{
		Addr:              env("HTTP_ADDR", ":8081"),
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	go func() {
		logger.Info("http listening", "addr", httpSrv.Addr, "local", isLocal)
		if err := httpSrv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			logger.Error("http serve", "err", err)
			stop()
		}
	}()

	<-ctx.Done()
	logger.Info("shutting down")
	shutCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := httpSrv.Shutdown(shutCtx); err != nil {
		logger.Error("http shutdown", "err", err)
	}
}

// runHeartbeat feeds the actuator watchdog while the process is healthy.
func runHeartbeat(ctx context.Context, d *actuator.Daemon, interval time.Duration) {
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			d.Heartbeat()
		}
	}
}

// TODO: Replace this stub with the proper operator queue, need to add to API.!!!
type stubOperatorQueue struct{ log *slog.Logger }

func (s *stubOperatorQueue) Enqueue(a advisory.Advisory) error {
	s.log.Info("OPERATOR-QUEUE <- advisory (stub)", "source", a.SourceID, "kind", a.Kind)
	return nil
}
