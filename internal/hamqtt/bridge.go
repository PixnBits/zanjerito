package hamqtt

import (
	"bytes"
	"context"
	"errors"
	"math/rand"
	"slices"
	"sync"
	"time"
)

// Bridge polls GET /api/ha and publishes Home Assistant MQTT discovery + state.
// It is testable without a broker via Publisher and Fetch.
type Bridge struct {
	Pub     Publisher
	Fetch   func(ctx context.Context) ([]byte, error)
	Version string
	Now     func() time.Time
	Refresh time.Duration
	Delay   func() time.Duration

	mu            sync.Mutex
	lastAvail     string
	lastState     []byte
	lastStateAt   time.Time
	lastZones     []string
	discPublished bool
	lastGood      []byte
	pollOK        bool // last Poll fetched a valid body
}

// Poll fetches a snapshot. A bad fetch or body publishes retained offline
// (once until recovery) and does not publish state. A good body publishes
// discovery if the zone set changed, state if the bytes changed or Refresh
// elapsed, then online. Order: discovery, state, availability.
func (b *Bridge) Poll(ctx context.Context) {
	var (
		body []byte
		err  error
	)
	if b.Fetch == nil {
		err = errNoFetch
	} else {
		body, err = b.Fetch(ctx)
	}

	b.mu.Lock()
	defer b.mu.Unlock()
	if err != nil {
		b.pollOK = false
		b.publishAvailLocked(PayloadOffline, false)
		return
	}
	if _, ok := ParseState(body); !ok {
		b.pollOK = false
		b.publishAvailLocked(PayloadOffline, false)
		return
	}
	b.pollOK = true
	b.lastGood = bytes.Clone(body)
	b.publishGoodLocked(body, false)
}

// OnConnect clears last-published caches (not lastGood) and republishes.
// Called from the broker OnConnect, including after reconnect.
func (b *Bridge) OnConnect() {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.forgetPublishedLocked()
	b.republishLocked()
}

// OnHAStatus republishes after Home Assistant's MQTT birth message.
// Other payloads are ignored. Delay (0–2s in production) staggers the burst.
func (b *Bridge) OnHAStatus(payload []byte) {
	if string(payload) != PayloadOnline {
		return
	}
	if d := b.delay(); d > 0 {
		time.Sleep(d)
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	b.republishLocked()
}

// republishLocked forces discovery, the last good state, and availability.
// Availability is online only if the latest poll succeeded; after a failed
// poll HA still gets the device and last state, but entities stay unavailable.
func (b *Bridge) republishLocked() {
	if len(b.lastGood) == 0 {
		b.publishAvailLocked(PayloadOffline, true)
		return
	}
	b.publishDiscStateLocked(b.lastGood, true)
	if b.pollOK {
		b.publishAvailLocked(PayloadOnline, true)
		return
	}
	b.publishAvailLocked(PayloadOffline, true)
}

// Shutdown publishes retained offline (clean stop).
func (b *Bridge) Shutdown() {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.publishAvailLocked(PayloadOffline, true)
}

func (b *Bridge) forgetPublishedLocked() {
	b.lastAvail = ""
	b.lastState = nil
	b.lastStateAt = time.Time{}
	b.lastZones = nil
	b.discPublished = false
}

func (b *Bridge) publishGoodLocked(body []byte, force bool) {
	b.publishDiscStateLocked(body, force)
	b.publishAvailLocked(PayloadOnline, force)
}

func (b *Bridge) publishDiscStateLocked(body []byte, force bool) {
	zones, _ := ParseState(body)
	needDisc := force || !b.discPublished || !slices.Equal(b.lastZones, zones)
	if needDisc {
		var removed []string
		if b.discPublished {
			removed = zoneDiff(b.lastZones, zones)
		}
		payload, err := Discovery(b.Version, zones, removed)
		if err == nil && b.publishLocked(TopicDiscovery, 1, true, payload) {
			b.lastZones = slices.Clone(zones)
			b.discPublished = true
		}
	}
	needState := force || !bytes.Equal(b.lastState, body) || b.refreshDueLocked()
	if needState {
		if b.publishLocked(TopicState, 1, true, body) {
			b.lastState = bytes.Clone(body)
			b.lastStateAt = b.now()
		}
	}
}

func (b *Bridge) publishAvailLocked(payload string, force bool) {
	if !force && b.lastAvail == payload {
		return
	}
	if b.publishLocked(TopicAvailability, 1, true, []byte(payload)) {
		b.lastAvail = payload
	}
}

func (b *Bridge) publishLocked(topic string, qos byte, retained bool, payload []byte) bool {
	if b.Pub == nil {
		return false
	}
	if err := b.Pub.Publish(topic, qos, retained, payload); err != nil {
		return false
	}
	return true
}

func (b *Bridge) refreshDueLocked() bool {
	if b.Refresh <= 0 || b.lastStateAt.IsZero() {
		return false
	}
	return !b.now().Before(b.lastStateAt.Add(b.Refresh))
}

func (b *Bridge) now() time.Time {
	if b.Now != nil {
		return b.Now()
	}
	return time.Now()
}

func (b *Bridge) delay() time.Duration {
	if b.Delay != nil {
		return b.Delay()
	}
	return time.Duration(rand.Int63n(int64(2 * time.Second)))
}

func zoneDiff(old, cur []string) []string {
	have := make(map[string]struct{}, len(cur))
	for _, z := range cur {
		have[z] = struct{}{}
	}
	var out []string
	for _, z := range old {
		if _, ok := have[z]; !ok {
			out = append(out, z)
		}
	}
	return out
}

var errNoFetch = errors.New("no fetch")
