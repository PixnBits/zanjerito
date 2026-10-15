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

const (
	healthNone = iota
	healthFetchErr
	healthBadBody
	healthOK
)

// Bridge polls GET /api/ha and publishes Home Assistant MQTT discovery + state.
// It is testable without a broker via Publisher and Fetch.
//
// Lock order is pubMu then mu. Publish runs without mu so a broker wait does
// not hold the state lock; pubMu keeps publishes in order. Publisher.Publish
// must not call back into Bridge.
type Bridge struct {
	Pub     Publisher
	Fetch   func(ctx context.Context) ([]byte, error)
	Version string
	Now     func() time.Time
	Refresh time.Duration
	Delay   func() time.Duration
	// Logf, when set, receives fetch, bad-body, and recovery transitions.
	// It must not call back into Bridge.
	Logf func(format string, args ...any)

	pubMu sync.Mutex
	mu    sync.Mutex

	lastAvail     string
	lastState     []byte
	lastStateAt   time.Time
	lastZones     []string
	discPublished bool
	lastGood      []byte
	pollOK        bool // last Poll fetched a valid body
	sawPoll       bool
	dirty         bool // a publish was skipped or failed; next good publish is forced
	health        int
	lastFetchErr  string
}

// Poll fetches a snapshot. A bad fetch or body publishes retained offline
// (once until recovery) when the broker is connected, and does not publish
// state. A good body publishes discovery if the zone set changed, state if
// the bytes changed or Refresh elapsed, then online. Order: discovery, state,
// availability.
//
// While the broker is disconnected, Poll still records lastGood and pollOK
// and does not call Publish. A failed publish leaves the matching cache
// unchanged and marks the bridge dirty so the next connected poll republishes.
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

	b.pubMu.Lock()
	defer b.pubMu.Unlock()
	b.mu.Lock()
	defer b.mu.Unlock()

	b.sawPoll = true
	if err != nil {
		b.noteFetchErr(err)
		b.pollOK = false
		b.publishOfflineLocked(false)
		return
	}
	if _, ok := ParseState(body); !ok {
		b.noteBadBody()
		b.pollOK = false
		b.publishOfflineLocked(false)
		return
	}
	b.noteGood()
	b.pollOK = true
	b.lastGood = bytes.Clone(body)
	b.publishGoodLocked(body, false)
}

// OnConnect clears last-published caches (not lastGood) and republishes.
// Called from the broker OnConnect, including after reconnect.
//
// With no good snapshot yet, it publishes nothing (the last-will retained
// offline covers a fresh start). If a poll has already failed, it publishes
// offline. Otherwise the order is discovery, latest state, availability
// (online only when the latest poll succeeded).
func (b *Bridge) OnConnect() {
	b.pubMu.Lock()
	defer b.pubMu.Unlock()
	b.mu.Lock()
	defer b.mu.Unlock()

	// Clearing caches is what makes an unchanged snapshot publish again.
	b.forgetPublishedLocked()
	if len(b.lastGood) == 0 {
		if b.sawPoll && !b.pollOK {
			b.finishDirty(b.publishAvailLocked(PayloadOffline, true))
		}
		return
	}
	discOK := b.publishDiscStateLocked(b.lastGood, false)
	var availOK bool
	if b.pollOK {
		availOK = b.publishAvailLocked(PayloadOnline, false)
	} else {
		availOK = b.publishAvailLocked(PayloadOffline, false)
	}
	b.finishDirty(discOK && availOK)
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
	b.pubMu.Lock()
	defer b.pubMu.Unlock()
	b.mu.Lock()
	defer b.mu.Unlock()
	b.republishLocked()
}

// republishLocked forces discovery, the last good state, and availability.
// Availability is online only if the latest poll succeeded; after a failed
// poll HA still gets the device and last state, but entities stay unavailable.
func (b *Bridge) republishLocked() {
	if !b.canPublish() {
		b.dirty = true
		return
	}
	if len(b.lastGood) == 0 {
		b.finishDirty(b.publishAvailLocked(PayloadOffline, true))
		return
	}
	discOK := b.publishDiscStateLocked(b.lastGood, true)
	var availOK bool
	if b.pollOK {
		availOK = b.publishAvailLocked(PayloadOnline, true)
	} else {
		availOK = b.publishAvailLocked(PayloadOffline, true)
	}
	b.finishDirty(discOK && availOK)
}

// Shutdown publishes retained offline when the broker connection is open.
func (b *Bridge) Shutdown() {
	b.pubMu.Lock()
	defer b.pubMu.Unlock()
	b.mu.Lock()
	defer b.mu.Unlock()
	if !b.canPublish() {
		return
	}
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
	if !b.canPublish() {
		b.dirty = true
		return
	}
	force = force || b.dirty
	discOK := b.publishDiscStateLocked(body, force)
	availOK := b.publishAvailLocked(PayloadOnline, force)
	b.finishDirty(discOK && availOK)
}

func (b *Bridge) publishOfflineLocked(force bool) {
	if !b.canPublish() {
		b.dirty = true
		return
	}
	// Offline does not cover discovery and state, so a dirty full republish
	// stays dirty until a good snapshot publish or OnConnect succeeds.
	if !b.publishAvailLocked(PayloadOffline, force || b.dirty) {
		b.dirty = true
	}
}

func (b *Bridge) publishDiscStateLocked(body []byte, force bool) bool {
	ok := true
	zones, _ := ParseState(body)
	needDisc := force || !b.discPublished || !slices.Equal(b.lastZones, zones)
	if needDisc {
		var removed []string
		if b.discPublished {
			removed = zoneDiff(b.lastZones, zones)
		}
		payload, err := Discovery(b.Version, zones, removed)
		if err != nil || !b.publishLocked(TopicDiscovery, 1, true, payload) {
			ok = false
		} else {
			b.lastZones = slices.Clone(zones)
			b.discPublished = true
		}
	}
	needState := force || !bytes.Equal(b.lastState, body) || b.refreshDueLocked()
	if needState {
		if b.publishLocked(TopicState, 1, true, body) {
			b.lastState = bytes.Clone(body)
			b.lastStateAt = b.now()
		} else {
			ok = false
		}
	}
	return ok
}

func (b *Bridge) publishAvailLocked(payload string, force bool) bool {
	if !b.canPublish() {
		return false
	}
	if !force && b.lastAvail == payload {
		return true
	}
	if !b.publishLocked(TopicAvailability, 1, true, []byte(payload)) {
		return false
	}
	b.lastAvail = payload
	return true
}

// publishLocked publishes one message. The caller holds mu and pubMu.
// mu is released for the broker call.
func (b *Bridge) publishLocked(topic string, qos byte, retained bool, payload []byte) bool {
	if b.Pub == nil || !b.Pub.Connected() {
		return false
	}
	pub := b.Pub
	b.mu.Unlock()
	err := pub.Publish(topic, qos, retained, payload)
	b.mu.Lock()
	return err == nil
}

func (b *Bridge) canPublish() bool {
	return b.Pub != nil && b.Pub.Connected()
}

func (b *Bridge) finishDirty(ok bool) {
	b.dirty = !ok
}

func (b *Bridge) noteFetchErr(err error) {
	msg := err.Error()
	if b.health != healthFetchErr || b.lastFetchErr != msg {
		b.logf("ha fetch: %s", msg)
		b.lastFetchErr = msg
	}
	b.health = healthFetchErr
}

func (b *Bridge) noteBadBody() {
	if b.health != healthBadBody {
		b.logf("ha body invalid: not a JSON object with phase")
	}
	b.health = healthBadBody
}

func (b *Bridge) noteGood() {
	if b.health == healthFetchErr || b.health == healthBadBody {
		b.logf("ha fetch: recovered")
	}
	b.health = healthOK
	b.lastFetchErr = ""
}

func (b *Bridge) logf(format string, args ...any) {
	if b.Logf != nil {
		b.Logf(format, args...)
	}
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
