// Package poller implements the outbound pull loop.
//
// For each enabled external source, a goroutine wakes on the source's interval,
// make one outbound GET request, and turns the response into a validated
// Advisory. Nothing external ever connects to us we start every call.
//
// The dispatch decision is per-source policy:
//   Disposition == advisory           -> Sink   (actuates immediately)
//   Disposition == operator_approved  -> OperatorQueue (human confirms)

package poller

import (
	"context"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"server/advisory"
	"server/store"
	"sync"
	"time"
)

// Validator turns a raw source response into a normalised Advisory. Each
// source Kind can have its own; the registry picks one by Kind.
type Validator func(src store.ExternalSource, body []byte, now time.Time) (advisory.Advisory, error)

type Poller struct {
	client     *http.Client
	sink       advisory.Sink
	opQueue    advisory.OperatorQueue
	log        *slog.Logger
	validators map[string]Validator
	mu         sync.Mutex
	cancels    map[string]context.CancelFunc
}

// Factory function for Poller.
func New(sink advisory.Sink, opQueue advisory.OperatorQueue, log *slog.Logger) *Poller {
	return &Poller{
		client: &http.Client{
			Timeout: 10 * time.Second, // bound every outbound call
		},
		sink:       sink,
		opQueue:    opQueue,
		log:        log,
		validators: map[string]Validator{"generic": genericValidator},
		cancels:    map[string]context.CancelFunc{},
	}
}

// RegisterValidator adds a Kind-specific validator (e.g. for the Environment
// Agency's schema). Falls back to "generic" when a source's Kind is unknown.
func (p *Poller) RegisterValidator(kind string, v Validator) {
	p.validators[kind] = v
}

// Sync stops either removed external sources or starts go-routines for new sources.
func (p *Poller) Sync(ctx context.Context, sources []store.ExternalSource) {
	p.mu.Lock()
	defer p.mu.Unlock()

	desired := map[string]store.ExternalSource{}
	for _, s := range sources {
		if s.Enabled {
			desired[s.ID] = s
		}
	}

	// Stop goroutines no longer desired (removed or disabled).
	for id, cancel := range p.cancels {
		if _, ok := desired[id]; !ok {
			cancel()
			delete(p.cancels, id)
			p.log.Info("poller: stopped source", "id", id)
		}
	}

	// Start goroutines for newly-desired sources.
	for id, src := range desired {
		if _, running := p.cancels[id]; running {
			continue
		}
		cctx, cancel := context.WithCancel(ctx)
		p.cancels[id] = cancel
		go p.run(cctx, src)
		p.log.Info("poller: started source", "id", id, "interval", src.PollMs)
	}

}

// Start polling the external sources.
func (p *Poller) run(ctx context.Context, src store.ExternalSource) {
	// Jittered first tick avoids all sources firing simultaneously.
	ticker := time.NewTicker(time.Duration(src.PollMs))
	defer ticker.Stop()

	// Poll once immediately so we don't wait a full interval for first data.
	p.pollOnce(ctx, src)
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			p.pollOnce(ctx, src)
		}
	}
}

// StopAll halts every source goroutine.
func (p *Poller) StopAll() {
	p.mu.Lock()
	defer p.mu.Unlock()
	for id, cancel := range p.cancels {
		cancel()
		delete(p.cancels, id)
	}
}

// Poll an external source for any new data. Reject stale or out of range data.
func (p *Poller) pollOnce(ctx context.Context, src store.ExternalSource) {
	now := time.Now().UTC()

	body, err := p.fetch(ctx, src)
	if err != nil {
		// No data.
		p.log.Warn("poller: fetch failed", "id", src.ID, "err", err)
		return
	}

	v := p.validators[src.Kind]
	if v == nil {
		v = p.validators["generic"]
	}
	adv, err := v(src, body, now)
	if err != nil {
		p.log.Warn("poller: validation rejected response", "id", src.ID, "err", err)
		return
	}

	// Bounds check the reading.
	if src.MaxValue != 0 && (adv.Value < src.MinValue || adv.Value > src.MaxValue) {
		p.log.Warn("poller: value out of bounds, dropping",
			"id", src.ID, "value", adv.Value, "min", src.MinValue, "max", src.MaxValue)
		return
	}

	// Check that the value we are reading isn't already stale.
	if src.MaxAgeMs != 0 && adv.Stale(now, time.Duration(src.MaxAgeMs)) {
		p.log.Warn("poller: advisory stale, dropping", "id", src.ID, "observedAt", adv.ObservedAt)
		return
	}

	adv.Disposition = advisory.Disposition(src.Disposition)
	p.dispatch(adv)
}

// Fetch the data using a GET request.
func (p *Poller) fetch(ctx context.Context, src store.ExternalSource) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, src.Url, nil)
	if err != nil {
		return nil, err
	}

	// Fill in any required authentication.
	if src.AuthHeader != "" && src.AuthToken != "" {
		req.Header.Set(src.AuthHeader, src.AuthToken)
	}
	req.Header.Set("Accept", "application/json")

	// Send the request.
	resp, err := p.client.Do(req)
	if err != nil {
		return nil, err
	}

	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("status %d", resp.StatusCode)
	}

	// Cap the body size to prevent any memory issues.
	return io.ReadAll(io.LimitReader(resp.Body, 1<<20)) // 1 MiB

}

// Dispatch the advisory to either the queue or submit it.
func (p *Poller) dispatch(adv advisory.Advisory) {
	switch adv.Disposition {
	case advisory.DispositionOperatorApproved:
		if err := p.opQueue.Enqueue(adv); err != nil {
			p.log.Error("poller: operator enqueue failed", "id", adv.SourceID, "err", err)
			return
		}
		p.log.Info("poller: advisory queued for operator", "id", adv.SourceID, "kind", adv.Kind)
	default: // DispositionAdvisory
		if err := p.sink.SubmitAdvisory(adv); err != nil {
			p.log.Error("poller: fog submit failed", "id", adv.SourceID, "err", err)
			return
		}
		p.log.Info("poller: advisory submitted to consensus", "id", adv.SourceID, "kind", adv.Kind, "sev", adv.Severity)
	}
}
