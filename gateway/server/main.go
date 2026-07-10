// Testing stub for checking message format from Chirpstack sent over MQTT.
// Since flutter only runs when someone is connect or flutter is running we need
// an intermediate to collect data, and then pass it onto flutter when someone
// starts up flutter/web app.
// This server application bridges that gap chirpstack -> server -> flutter
// For testing, I just make a simple websocket so flutter get's a live update of incoming messages.

package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/gorilla/websocket"
)

const (
	historySize     = 100 // ring buffer of recent uplinks
	mqttTopic       = "application/+/device/+/event/up"
	wsWriteTimeout  = 5 * time.Second
	wsClientBufSize = 32 // per-client send queue depth
)

// Uplink is the subset of ChirpStack's uplink event we forward to flutter. We
// pass through the decoded `object` from the codec verbatim because that's the
// actual telemetry; everything else is metadata useful for debugging.
type Uplink struct {
	ReceivedAt    time.Time              `json:"receivedAt"`
	DevEUI        string                 `json:"devEui"`
	DeviceName    string                 `json:"deviceName,omitempty"`
	ApplicationID string                 `json:"applicationId,omitempty"`
	FCnt          uint32                 `json:"fCnt,omitempty"`
	FPort         uint8                  `json:"fPort,omitempty"`
	Object        map[string]interface{} `json:"object,omitempty"` // decoded payload
	RxRSSI        *int                   `json:"rssi,omitempty"`
	RxSNR         *float64               `json:"snr,omitempty"`
}

// ChirpStack v4 event-up envelope, only the fields we care about.
type chirpstackUplink struct {
	DeviceInfo struct {
		DevEUI        string `json:"devEui"`
		DeviceName    string `json:"deviceName"`
		ApplicationID string `json:"applicationId"`
	} `json:"deviceInfo"`
	FCnt   uint32                 `json:"fCnt"`
	FPort  uint8                  `json:"fPort"`
	Object map[string]interface{} `json:"object"`
	RxInfo []struct {
		RSSI int     `json:"rssi"`
		SNR  float64 `json:"snr"`
	} `json:"rxInfo"`
}

// Hub holds the connected clients and the ring buffer of recent uplinks.
type Hub struct {
	mu      sync.Mutex
	clients map[chan []byte]struct{}
	history []Uplink // ring; oldest first
	head    int      // next write position
	filled  bool
}

func newHub() *Hub {
	return &Hub{
		clients: make(map[chan []byte]struct{}),
		history: make([]Uplink, historySize),
	}
}

func (h *Hub) record(u Uplink) []byte {
	msg, err := json.Marshal(u)
	if err != nil {
		log.Printf("marshal uplink: %v", err)
		return nil
	}
	h.mu.Lock()
	h.history[h.head] = u
	h.head = (h.head + 1) % historySize
	if h.head == 0 {
		h.filled = true
	}
	// Snapshot client channels so we don't hold the lock during sends.
	chans := make([]chan []byte, 0, len(h.clients))
	for c := range h.clients {
		chans = append(chans, c)
	}
	h.mu.Unlock()

	for _, c := range chans {
		select {
		case c <- msg:
		default:
			// Slow client, drop the message rather than block.
			// The client will reconnect or accept the loss.
		}
	}
	return msg
}

func (h *Hub) snapshot() []Uplink {
	h.mu.Lock()
	defer h.mu.Unlock()
	if !h.filled {
		out := make([]Uplink, h.head)
		copy(out, h.history[:h.head])
		return out
	}
	out := make([]Uplink, historySize)
	copy(out, h.history[h.head:])
	copy(out[historySize-h.head:], h.history[:h.head])
	return out
}

// Add a client listening to the websocket.
func (h *Hub) addClient() chan []byte {
	c := make(chan []byte, wsClientBufSize)
	h.mu.Lock()
	h.clients[c] = struct{}{}
	h.mu.Unlock()
	return c
}

func (h *Hub) removeClient(c chan []byte) {
	h.mu.Lock()
	delete(h.clients, c)
	h.mu.Unlock()
	close(c)
}

func env(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func main() {
	hub := newHub()

	// MQTT client running remotely on the PI
	opts := mqtt.NewClientOptions().
		AddBroker(env("MQTT_BROKER", "tcp://192.168.1.109:1883")).
		SetClientID("telemetry-bridge").
		SetCleanSession(true).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(2 * time.Second).
		SetOnConnectHandler(func(c mqtt.Client) {
			log.Printf("MQTT connected; subscribing to %s", mqttTopic)
			if t := c.Subscribe(mqttTopic, 0, func(_ mqtt.Client, m mqtt.Message) {
				handleUplink(hub, m.Payload())
			}); t.Wait() && t.Error() != nil {
				log.Fatalf("subscribe: %v", t.Error())
			}
		}).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			log.Printf("MQTT connection lost: %v", err)
		})

	user := env("MQTT_USERNAME", "")
	if user != "" {
		opts.SetUsername(user).SetPassword(env("MQTT_PASSWORD", ""))
	}

	client := mqtt.NewClient(opts)
	if t := client.Connect(); t.Wait() && t.Error() != nil {
		log.Fatalf("MQTT connect: %v", t.Error())
	}

	// HTTP / WebSocket
	upgrader := websocket.Upgrader{
		CheckOrigin: func(_ *http.Request) bool { return true },
	}

	// Handle healthz endpoint
	http.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("ok"))
	})

	// Encode the recent snapshot and sent it to the client.
	http.HandleFunc("/recent", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(hub.snapshot())
	})

	// Handle websocket.
	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Printf("ws upgrade: %v", err)
			return
		}
		log.Printf("ws client connected: %s", r.RemoteAddr)
		serveWS(hub, conn)
	})

	// Run the HTTP server.
	addr := env("HTTP_ADDR", ":8081")
	log.Printf("HTTP listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}

// Handle a new uplink message from chirpstack.
func handleUplink(hub *Hub, payload []byte) {
	var cs chirpstackUplink
	if err := json.Unmarshal(payload, &cs); err != nil {
		log.Printf("parse uplink: %v", err)
		return
	}

	u := Uplink{
		ReceivedAt:    time.Now().UTC(),
		DevEUI:        cs.DeviceInfo.DevEUI,
		DeviceName:    cs.DeviceInfo.DeviceName,
		ApplicationID: cs.DeviceInfo.ApplicationID,
		FCnt:          cs.FCnt,
		FPort:         cs.FPort,
		Object:        cs.Object,
	}
	// Take best RSSI/SNR across gateways that heard the uplink.
	if len(cs.RxInfo) > 0 {
		bestRSSI := cs.RxInfo[0].RSSI
		bestSNR := cs.RxInfo[0].SNR
		for _, rx := range cs.RxInfo[1:] {
			if rx.RSSI > bestRSSI {
				bestRSSI = rx.RSSI
			}
			if rx.SNR > bestSNR {
				bestSNR = rx.SNR
			}
		}
		u.RxRSSI = &bestRSSI
		u.RxSNR = &bestSNR
	}

	hub.record(u)
	log.Printf("uplink from %s: fCnt=%d count=%v",
		u.DevEUI, u.FCnt, u.Object["count"])
}

func serveWS(hub *Hub, conn *websocket.Conn) {
	ch := hub.addClient()
	defer func() {
		hub.removeClient(ch)
		conn.Close()
	}()

	// Send recent history on connect.
	for _, u := range hub.snapshot() {
		msg, err := json.Marshal(u)
		if err != nil {
			continue
		}
		conn.SetWriteDeadline(time.Now().Add(wsWriteTimeout))
		if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
			return
		}
	}

	// We don't expect messages from the client.
	// Just close the connection if they disconnect.
	go func() {
		for {
			if _, _, err := conn.NextReader(); err != nil {
				conn.Close()
				return
			}
		}
	}()

	// Writer loop: pull messages off the per-client channel.
	for msg := range ch {
		conn.SetWriteDeadline(time.Now().Add(wsWriteTimeout))
		if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
			return
		}
	}
}
