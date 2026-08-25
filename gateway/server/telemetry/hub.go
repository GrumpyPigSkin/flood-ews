package telemetry

import (
	"encoding/json"
	"log/slog"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// Hub holds connected WebSocket clients and a ring buffer of recent uplinks.
// New clients receive the buffered history on connect so a freshly-opened
// dashboard sees recent data immediately rather than waiting for the next
// uplink (which over LoRaWAN can be minutes away).
type Hub struct {
	mu       sync.Mutex
	clients  map[chan []byte]struct{}
	history  []Uplink
	head     int
	filled   bool
	capacity int

	writeTimeout time.Duration
	clientBuf    int
	log          *slog.Logger
}

// Factory function for a new Hub.
func NewHub(cfg Config, log *slog.Logger) *Hub {
	cfg = cfg.withDefaults()
	return &Hub{
		clients:      make(map[chan []byte]struct{}),
		history:      make([]Uplink, cfg.HistorySize),
		capacity:     cfg.HistorySize,
		writeTimeout: cfg.WSWriteTimeout,
		clientBuf:    cfg.WSClientBuf,
		log:          log,
	}
}

// Check if this is the first time we have seen this uplink.
func (h *Hub) IsUnique(u Uplink) bool {

	seq, ok := getSeq(u)

	if !ok {
		return true
	}

	h.mu.Lock()
	defer h.mu.Unlock()

	for i := range h.history {
		if h.history[i].Object == nil {
			continue
		}

		if prevSeq, ok := getSeq(h.history[i]); ok && prevSeq == seq {
			return false
		}
	}

	return true
}

// Record buffers an uplink and broadcasts it to every connected client. Slow
// clients that can't keep up have the message dropped rather than blocking the
// broadcast.
func (h *Hub) Record(u Uplink) {

	msg, err := json.Marshal(u)
	if err != nil {
		h.log.Warn("marshal uplink", "err", err)
		return
	}

	h.mu.Lock()

	h.history[h.head] = u
	h.head = (h.head + 1) % h.capacity
	if h.head == 0 {
		h.filled = true
	}

	chans := make([]chan []byte, 0, len(h.clients))
	for c := range h.clients {
		chans = append(chans, c)
	}

	h.mu.Unlock()

	for _, c := range chans {
		select {
		case c <- msg:
		default:
		}
	}
}

// Snapshot returns the buffered uplinks oldest-first.
func (h *Hub) Snapshot() []Uplink {
	h.mu.Lock()
	defer h.mu.Unlock()
	if !h.filled {
		out := make([]Uplink, h.head)
		copy(out, h.history[:h.head])
		return out
	}
	out := make([]Uplink, h.capacity)
	copy(out, h.history[h.head:])
	copy(out[h.capacity-h.head:], h.history[:h.head])
	return out
}

// Add a client to listen to the websocket
func (h *Hub) addClient() chan []byte {
	c := make(chan []byte, h.clientBuf)
	h.mu.Lock()
	h.clients[c] = struct{}{}
	h.mu.Unlock()
	return c
}

// Remove a client.
func (h *Hub) removeClient(c chan []byte) {
	h.mu.Lock()
	delete(h.clients, c)
	h.mu.Unlock()
	close(c)
}

// ServeWS streams to one WebSocket client: buffered history first, then live
// uplinks as they arrive. Returns when the client disconnects.
func (h *Hub) ServeWS(conn *websocket.Conn) {

	ch := h.addClient()
	defer func() {
		h.removeClient(ch)
		conn.Close()
	}()

	// Replay recent history on connect.
	for _, u := range h.Snapshot() {
		msg, err := json.Marshal(u)
		if err != nil {
			continue
		}
		conn.SetWriteDeadline(time.Now().Add(h.writeTimeout))
		if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
			return
		}
	}

	// Reader goroutine: only to detect disconnect. We don't expect client
	// messages, a read error closes the conn, which fails the next write.
	go func() {
		for {
			if _, _, err := conn.NextReader(); err != nil {
				conn.Close()
				return
			}
		}
	}()

	// Writer loop: drain the per-client channel.
	for msg := range ch {
		conn.SetWriteDeadline(time.Now().Add(h.writeTimeout))
		if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
			return
		}
	}
}

// Extract the sequence number from the uplink.
func getSeq(u Uplink) (int64, bool) {
	if u.Object == nil {
		return 0, false
	}

	v := u.Object["seq"].(int64)
	return v, true
}
