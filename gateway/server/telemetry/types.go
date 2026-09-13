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

// A single sensor entry.
type SensorEntry struct {
	DeviceEUI    string   `json:"device_eui"`
	WaterLevelMM FlexUint `json:"water_level_mm"`
	Validity     string   `json:"validity"`
	ValidityCode FlexUint `json:"validity_code"`
	Outlier      bool     `json:"outlier"`
	Detail       FlexUint `json:"detail"`
	Timestamp    FlexUint `json:"timestamp"`
}

// Data is the data we set in the codec that comes inside the ChirpStack uplink.
type Data struct {
	NodeId       string        `json:"node_id"`
	RaftTerm     FlexUint      `json:"raft_term"`
	RaftLogIndex FlexUint      `json:"raft_log_index"`
	Seq          FlexUint      `json:"seq"`
	Alert        bool          `json:"alert"`
	Count        FlexUint      `json:"count"`
	Entries      []SensorEntry `json:"entries"`
}

// Uplink is the subset of ChirpStack's uplink event forwarded to clients. The
// decoded `object` is passed through verbatim because that is the actual
// telemetry, the rest is metadata useful for display and debugging.
type Uplink struct {
	ReceivedAt    time.Time `json:"receivedAt"`
	DevEUI        string    `json:"devEui"`
	DeviceName    string    `json:"deviceName,omitempty"`
	ApplicationID string    `json:"applicationId,omitempty"`
	FCnt          uint32    `json:"fCnt,omitempty"`
	FPort         uint8     `json:"fPort,omitempty"`
	Object        Data      `json:"object,omitempty"`
	RxRSSI        *int      `json:"rssi,omitempty"`
	RxSNR         *float64  `json:"snr,omitempty"`
}

// chirpstackUplink is the ChirpStack v4 event-up envelope.
type chirpstackUplink struct {
	DeviceInfo struct {
		DevEUI        string `json:"devEui"`
		DeviceName    string `json:"deviceName"`
		ApplicationID string `json:"applicationId"`
	} `json:"deviceInfo"`
	FCnt   uint32 `json:"fCnt"`
	FPort  uint8  `json:"fPort"`
	Data   Data   `json:"object"`
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
		Object:        cs.Data,
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

	if u.Object.Alert {
		return advisory.SeverityCritical
	}

	return advisory.SeverityInfo
}

// kind classifies the uplink for egress (e.g. "reading", "status").
func (u Uplink) kind() string {
	return "sensor_reading"
}

// Get the mean across good readings.
func (u Uplink) mean() int {
	if len(u.Object.Entries) == 0 {
		return 0
	}

	var sum int
	var count int

	for _, entry := range u.Object.Entries {
		if entry.Validity == "GOOD" {
			sum += entry.WaterLevelMM.Int()
			count++
		}
	}

	if count == 0 {
		return 0
	}

	return sum / count
}

// sensors extracts per-sensor readings from object.entries[], keyed by
// device_eui, for folding into the live store.
func (u Uplink) sensors() map[string]SensorEntry {
	out := make(map[string]SensorEntry, len(u.Object.Entries))

	for _, e := range u.Object.Entries {
		if e.DeviceEUI != "" {
			out[e.DeviceEUI] = e
		}
	}

	return out
}
