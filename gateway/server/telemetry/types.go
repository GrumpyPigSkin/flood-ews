// Package telemetry MQTT uplinks to WebSocket clients and fans each uplink out
// to the live store and egress sinks. It is the gateway's ingestion edge:
// LoRaWAN telemetry arrives here as MQTT, is normalised, broadcast to
// dashboards over WebSocket, mirrored for REST reads, and pushed to the cloud
// read-model / EWS authority.

package telemetry

import (
	"server/advisory"
	"time"
)

type Config struct {
	HistorySize    int           // Ring buffer size for recent uplinks
	MQTTTopic      string        // ChirpStack uplink topic
	WSWriteTimeout time.Duration // per-write deadline for WS sends
	WSClientBuf    int           // per-client send queue depth
}

func (c Config) withDefaults() Config {
	if c.HistorySize <= 0 {
		c.HistorySize = 100
	}
	if c.MQTTTopic == "" {
		c.MQTTTopic = "application/+/device/+/event/up"
	}
	if c.WSWriteTimeout <= 0 {
		c.WSWriteTimeout = 5 * time.Second
	}
	if c.WSClientBuf <= 0 {
		c.WSClientBuf = 32
	}
	return c
}

// Uplink is the subset of ChirpStack's uplink event forwarded to clients. The
// decoded `object` is passed through verbatim because that is the actual
// telemetry, the rest is metadata useful for display and debugging.
type Uplink struct {
	ReceivedAt    time.Time              `json:"receivedAt"`
	DevEUI        string                 `json:"devEui"`
	DeviceName    string                 `json:"deviceName,omitempty"`
	ApplicationID string                 `json:"applicationId,omitempty"`
	FCnt          uint32                 `json:"fCnt,omitempty"`
	FPort         uint8                  `json:"fPort,omitempty"`
	Object        map[string]interface{} `json:"object,omitempty"`
	RxRSSI        *int                   `json:"rssi,omitempty"`
	RxSNR         *float64               `json:"snr,omitempty"`
}

// chirpstackUplink is the ChirpStack v4 event-up envelope.
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

// toUplink converts a parsed ChirpStack envelope into our Uplink, taking the
// best RSSI/SNR across gateways that heard the uplink.
func (cs chirpstackUplink) toUplink(now time.Time) Uplink {

	u := Uplink{
		ReceivedAt:    now,
		DevEUI:        cs.DeviceInfo.DevEUI,
		DeviceName:    cs.DeviceInfo.DeviceName,
		ApplicationID: cs.DeviceInfo.ApplicationID,
		FCnt:          cs.FCnt,
		FPort:         cs.FPort,
		Object:        cs.Object,
	}

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
	return u
}

// severity maps an uplink's decoded payload onto the normalised severity
// vocabulary, an alert is critical.
func (u Uplink) severity() advisory.Severity {

	if alert, _ := u.Object["alert"].(bool); alert {
		return advisory.SeverityCritical
	}

	return advisory.SeverityInfo
}

// kind classifies the uplink for egress (e.g. "reading", "status").
func (u Uplink) kind() string {

	if t, ok := u.Object["type"].(string); ok && t != "" {
		return t
	}

	return "reading"
}

// stations extracts per-station readings from object.entries[], keyed by
// device_eui, for folding into the live store.
func (u Uplink) stations() map[string]map[string]interface{} {

	out := map[string]map[string]interface{}{}
	entries, ok := u.Object["entries"].([]interface{})

	if !ok {
		return out
	}

	for _, e := range entries {
		m, ok := e.(map[string]interface{})
		if !ok {
			continue
		}
		if eui, _ := m["device_eui"].(string); eui != "" {
			out[eui] = m
		}
	}

	return out
}
