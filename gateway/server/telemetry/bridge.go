package telemetry

import (
	"context"
	"encoding/json"
	"log/slog"
	"server/advisory"
	"server/egress"
	"server/store"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/jackc/pgx/v5/pgtype"
)

// SensorSink receives per-sensor readings folded from uplinks (satisfied
// by api.LiveStore).
type SensorSink interface {
	UpdateSensor(eui string, reading SensorEntry)
}

// EgressSink buffers/pushes events to the cloud read-model (satisfied by a
// thin adapter over egress.SupabaseSink). Nil disables cloud egress.
type EgressSink interface {
	AppendBatch(ctx context.Context, events []egress.TelemetryEvent) error
}

// WebhookForwarder forwards significant events to the central EWS authority
// (satisfied by egress.WebhookSink). Nil disables webhook forwarding.
type WebhookForwarder interface {
	Forward(ctx context.Context, target store.EgressTarget, e egress.TelemetryEvent) error
}

// Bridge is the single fan-out point for an uplink: broadcast to WS clients,
// fold into the live store for REST, buffer for the cloud read-model, and
// forward alerts to the EWS authority. Keeping this in one place means one
// decision about what leaves the gateway.
type Bridge struct {
	hub          *Hub
	live         SensorSink
	egress       EgressSink
	advisorySink advisory.Sink
	webhook      WebhookForwarder
	targets      []store.EgressTarget
	log          *slog.Logger

	mu      sync.Mutex
	pending []egress.TelemetryEvent
}

// BridgeDeps groups the bridge's collaborators. egress/webhook may be nil.
type BridgeDeps struct {
	Hub          *Hub
	Live         SensorSink
	Egress       EgressSink
	AdvisorySink advisory.Sink
	Webhook      WebhookForwarder
	Targets      []store.EgressTarget
	Log          *slog.Logger
}

// Bridge factory function.
func NewBridge(d BridgeDeps) *Bridge {
	return &Bridge{
		hub:          d.Hub,
		live:         d.Live,
		egress:       d.Egress,
		advisorySink: d.AdvisorySink,
		webhook:      d.Webhook,
		targets:      d.Targets,
		log:          d.Log,
	}
}

// HandleUplink parses one ChirpStack MQTT payload and fans it out.
func (b *Bridge) HandleUplink(payload []byte) {
	var cs chirpstackUplink
	if err := json.Unmarshal(payload, &cs); err != nil {
		b.log.Warn("parse uplink", "err", err)
		return
	}
	u := cs.toUplink(time.Now().UTC())

	// Check for a duplicate entry.
	if !b.hub.IsUnique(u) {
		b.log.Error("Duplicate dropped", "Sequence ID", u.Object.Seq.Uint32())
		return
	}

	// Broadcast to WS clients.
	b.hub.Record(u)

	// Fold sensor entries into the live store for REST reads.
	if b.live != nil {
		for eui, reading := range u.sensors() {
			b.live.UpdateSensor(eui, reading)
		}
	}

	payloadBytes, err := json.Marshal(u.Object)
	if err != nil {
		return
	}

	advisory := advisory.Advisory{
		SourceID:   "local",
		Kind:       u.kind(),
		Severity:   u.severity(),
		Value:      float64(u.mean()),
		Unit:       "mm",
		ObservedAt: time.Now(),
		ReceivedAt: time.Now(),
	}

	b.advisorySink.SubmitAdvisory(advisory)

	// Egress: buffer for the read-model, forward alerts.
	ev := egress.TelemetryEvent{
		At: pgtype.Timestamptz{
			Time:  u.ReceivedAt,
			Valid: true,
		},
		Source:   u.DevEUI,
		Kind:     u.kind(),
		Severity: int32(u.severity()),
		Payload:  payloadBytes,
	}
	b.enqueueEgress(ev)

	b.log.Info("uplink", "devEui", u.DevEUI, "fCnt", u.FCnt, "severity", ev.Severity)
}

// enqueueEgress buffers an event for batched read-model append and forwards
// it to any webhook target meeting its severity threshold.
func (b *Bridge) enqueueEgress(ev egress.TelemetryEvent) {

	if b.egress != nil {
		b.mu.Lock()
		b.pending = append(b.pending, ev)
		b.mu.Unlock()
	}

	if b.webhook != nil {
		for _, t := range b.targets {
			if t.Enabled && store.EgressType(t.Type) == store.TypeWebhook {
				go func(target store.EgressTarget) {
					ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
					defer cancel()
					if err := b.webhook.Forward(ctx, target, ev); err != nil {
						b.log.Warn("egress webhook forward failed", "target", target.ID, "err", err)
					}
				}(t)
			}
		}
	}
}

// FlushEgress drains the buffer to the read-model in one batch. On failure
// the batch is re-queued so data isn't lost.
func (b *Bridge) FlushEgress(ctx context.Context) {
	if b.egress == nil {
		return
	}
	b.mu.Lock()
	if len(b.pending) == 0 {
		b.mu.Unlock()
		return
	}
	batch := b.pending
	b.pending = nil
	b.mu.Unlock()

	if err := b.egress.AppendBatch(ctx, batch); err != nil {
		b.log.Warn("egress read-model append failed", "count", len(batch), "err", err)
		b.mu.Lock()
		combined := make([]egress.TelemetryEvent, len(batch)+len(b.pending))
		copy(combined, batch)
		copy(combined[len(batch):], b.pending)
		b.pending = combined
		b.mu.Unlock()
	}
}

// RunEgressFlusher flushes on an interval until ctx is cancelled, then does a
// final flush. Run in its own goroutine.
func (b *Bridge) RunEgressFlusher(ctx context.Context, interval time.Duration) {
	if interval <= 0 {
		interval = 5 * time.Second
	}
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			b.FlushEgress(context.Background())
			return
		case <-t.C:
			b.FlushEgress(ctx)
		}
	}
}

// MQTTOptions holds broker connection settings.
type MQTTOptions struct {
	Broker   string
	ClientID string
	Username string
	Password string
	Topic    string
}

// ConnectMQTT builds and connects an MQTT client that routes uplinks on the
// configured topic to b.HandleUplink. Auto-reconnects. A connect failure is
// returned but is non-fatal to the caller: the bridge still serves cached
// data and the API still runs.
func (b *Bridge) ConnectMQTT(opts MQTTOptions) (mqtt.Client, error) {

	topic := opts.Topic
	if topic == "" {
		topic = Config{}.withDefaults().MQTTTopic
	}

	clientID := opts.ClientID
	if clientID == "" {
		clientID = "gateway-bridge"
	}

	mo := mqtt.NewClientOptions().
		AddBroker(opts.Broker).
		SetClientID(clientID).
		SetCleanSession(true).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(2 * time.Second).
		SetOnConnectHandler(func(c mqtt.Client) {
			b.log.Info("MQTT connected; subscribing", "topic", topic)
			if tok := c.Subscribe(topic, 0, func(_ mqtt.Client, m mqtt.Message) {
				b.HandleUplink(m.Payload())
			}); tok.Wait() && tok.Error() != nil {
				b.log.Error("subscribe", "err", tok.Error())
			}
		}).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			b.log.Warn("MQTT connection lost", "err", err)
		})

	if opts.Username != "" {
		mo.SetUsername(opts.Username).SetPassword(opts.Password)
	}

	client := mqtt.NewClient(mo)
	if tok := client.Connect(); tok.Wait() && tok.Error() != nil {
		return client, tok.Error()
	}

	return client, nil
}
