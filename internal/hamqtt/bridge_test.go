package hamqtt

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"os"
	"sync"
	"testing"
	"time"
)

type pubCall struct {
	Topic    string
	QoS      byte
	Retained bool
	Payload  []byte
}

type fakePub struct {
	mu    sync.Mutex
	calls []pubCall
	err   error
}

func (f *fakePub) Publish(topic string, qos byte, retained bool, payload []byte) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls = append(f.calls, pubCall{
		Topic:    topic,
		QoS:      qos,
		Retained: retained,
		Payload:  bytes.Clone(payload),
	})
	return f.err
}

func (f *fakePub) snapshot() []pubCall {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]pubCall, len(f.calls))
	copy(out, f.calls)
	return out
}

func (f *fakePub) topics() []string {
	var out []string
	for _, c := range f.snapshot() {
		out = append(out, c.Topic)
	}
	return out
}

func (f *fakePub) payloads(topic string) [][]byte {
	var out [][]byte
	for _, c := range f.snapshot() {
		if c.Topic == topic {
			out = append(out, c.Payload)
		}
	}
	return out
}

func (f *fakePub) reset() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls = nil
}

func testNow() time.Time {
	return time.Unix(1_700_000_000, 0).UTC()
}

func newTestBridge(pub *fakePub, fetch func(context.Context) ([]byte, error)) *Bridge {
	return &Bridge{
		Pub:     pub,
		Fetch:   fetch,
		Version: "test",
		Now:     testNow,
		Refresh: time.Hour,
		Delay:   func() time.Duration { return 0 },
	}
}

func fetchBody(body []byte, err error) func(context.Context) ([]byte, error) {
	return func(context.Context) ([]byte, error) { return body, err }
}

func requireRetainedQoS1(t *testing.T, calls []pubCall) {
	t.Helper()
	for _, c := range calls {
		if !c.Retained {
			t.Fatalf("%s not retained", c.Topic)
		}
		if c.QoS != 1 {
			t.Fatalf("%s qos %d", c.Topic, c.QoS)
		}
	}
}

func TestPollPublishOnChangeAndRefresh(t *testing.T) {
	idle := haFixture(t, "idle.json")
	running := haFixture(t, "running.json")
	pub := &fakePub{}
	var body []byte
	b := newTestBridge(pub, func(context.Context) ([]byte, error) { return body, nil })

	now := testNow()
	b.Now = func() time.Time { return now }
	b.Refresh = 5 * time.Minute

	body = idle
	b.Poll(context.Background())
	if got := pub.topics(); len(got) != 3 || got[0] != TopicDiscovery || got[1] != TopicState || got[2] != TopicAvailability {
		t.Fatalf("first poll topics %v", got)
	}
	requireRetainedQoS1(t, pub.snapshot())
	if !bytes.Equal(pub.payloads(TopicState)[0], idle) {
		t.Fatal("state not published verbatim")
	}
	if string(pub.payloads(TopicAvailability)[0]) != PayloadOnline {
		t.Fatal("want online")
	}

	n := len(pub.snapshot())
	body = append([]byte(nil), idle...)
	b.Poll(context.Background())
	if len(pub.snapshot()) != n {
		t.Fatalf("identical body republished: %v", pub.topics())
	}

	body = running
	b.Poll(context.Background())
	states := pub.payloads(TopicState)
	if len(states) != 2 || !bytes.Equal(states[1], running) {
		t.Fatalf("changed body: %d state publishes", len(states))
	}

	n = len(pub.snapshot())
	body = running
	now = now.Add(5 * time.Minute)
	b.Poll(context.Background())
	states = pub.payloads(TopicState)
	if len(states) != 3 || !bytes.Equal(states[2], running) {
		t.Fatalf("refresh elapsed: %d state publishes, extra calls %v", len(states), pub.topics()[n:])
	}
	avails := pub.payloads(TopicAvailability)
	if len(avails) != 1 {
		t.Fatalf("availability republished on refresh: %d", len(avails))
	}
}

func TestPollOfflineDecisions(t *testing.T) {
	cases := []struct {
		name string
		body []byte
		err  error
	}{
		{name: "invalid JSON", body: []byte(`{`)},
		{name: "JSON array", body: []byte(`[{"phase":"Idle"}]`)},
		{name: "JSON null", body: []byte(`null`)},
		{name: "missing phase", body: []byte(`{"paused":false}`)},
		{name: "unreachable", err: errors.New("unreachable")},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			pub := &fakePub{}
			b := newTestBridge(pub, fetchBody(tc.body, tc.err))
			b.Poll(context.Background())
			if got := pub.topics(); len(got) != 1 || got[0] != TopicAvailability {
				t.Fatalf("topics %v", got)
			}
			if string(pub.payloads(TopicAvailability)[0]) != PayloadOffline {
				t.Fatalf("payload %s", pub.payloads(TopicAvailability)[0])
			}
			if len(pub.payloads(TopicState)) != 0 {
				t.Fatal("must not publish state")
			}
			requireRetainedQoS1(t, pub.snapshot())

			b.Poll(context.Background())
			b.Poll(context.Background())
			if len(pub.payloads(TopicAvailability)) != 1 {
				t.Fatalf("offline published %d times", len(pub.payloads(TopicAvailability)))
			}

			idle := haFixture(t, "idle.json")
			b.Fetch = fetchBody(idle, nil)
			b.Poll(context.Background())
			if got := pub.topics(); len(got) != 4 {
				t.Fatalf("recovery topics %v", got)
			}
			tail := pub.snapshot()[1:]
			if tail[0].Topic != TopicDiscovery || tail[1].Topic != TopicState || tail[2].Topic != TopicAvailability {
				t.Fatalf("recovery order %v", pub.topics()[1:])
			}
			if string(tail[2].Payload) != PayloadOnline {
				t.Fatal("want online after recovery")
			}
			if !bytes.Equal(tail[1].Payload, idle) {
				t.Fatal("recovery state")
			}
		})
	}
}

func TestPollZoneSetChangePublishesRemoved(t *testing.T) {
	pub := &fakePub{}
	two := []byte(`{"phase":"Idle","zones":[{"id":"zone_1","on":false},{"id":"zone_2","on":false}]}`)
	one := []byte(`{"phase":"Idle","zones":[{"id":"zone_1","on":false}]}`)
	var body []byte
	b := newTestBridge(pub, func(context.Context) ([]byte, error) { return body, nil })

	body = two
	b.Poll(context.Background())
	body = one
	b.Poll(context.Background())

	raws := pub.payloads(TopicDiscovery)
	if len(raws) != 2 {
		t.Fatalf("discovery count %d", len(raws))
	}
	var root map[string]any
	if err := json.Unmarshal(raws[1], &root); err != nil {
		t.Fatal(err)
	}
	cmps := cmpsOf(t, root)
	if _, ok := cmps["zone_1"]; !ok {
		t.Fatal("zone_1 missing")
	}
	rem := cmps["zone_2"]
	if rem == nil || len(rem) != 1 || rem["p"] != "binary_sensor" {
		t.Fatalf("removed zone_2 %+v", rem)
	}
}

func TestOnHAStatus(t *testing.T) {
	idle := haFixture(t, "idle.json")
	pub := &fakePub{}
	b := newTestBridge(pub, fetchBody(idle, nil))
	b.Poll(context.Background())
	pub.reset()

	b.OnHAStatus([]byte("offline"))
	b.OnHAStatus([]byte("ONLINE"))
	b.OnHAStatus([]byte("online\n"))
	if len(pub.snapshot()) != 0 {
		t.Fatalf("ignored payloads published %v", pub.topics())
	}

	var delayed bool
	b.Delay = func() time.Duration {
		delayed = true
		return 0
	}
	b.OnHAStatus([]byte(PayloadOnline))
	if !delayed {
		t.Fatal("Delay not called")
	}
	if got := pub.topics(); len(got) != 3 || got[0] != TopicDiscovery || got[1] != TopicState || got[2] != TopicAvailability {
		t.Fatalf("HA online topics %v", got)
	}
	if string(pub.payloads(TopicAvailability)[0]) != PayloadOnline {
		t.Fatal("want online")
	}
	if !bytes.Equal(pub.payloads(TopicState)[0], idle) {
		t.Fatal("state")
	}
}

func TestOnHAStatusWithoutLastGoodPublishesOffline(t *testing.T) {
	pub := &fakePub{}
	b := newTestBridge(pub, fetchBody(nil, errors.New("unreachable")))
	b.OnHAStatus([]byte(PayloadOnline))
	if got := pub.topics(); len(got) != 1 || got[0] != TopicAvailability {
		t.Fatalf("topics %v", got)
	}
	if string(pub.payloads(TopicAvailability)[0]) != PayloadOffline {
		t.Fatal("want offline")
	}
}

func TestOnConnect(t *testing.T) {
	pub := &fakePub{}
	b := newTestBridge(pub, nil)
	b.OnConnect()
	if got := pub.topics(); len(got) != 1 || string(pub.payloads(TopicAvailability)[0]) != PayloadOffline {
		t.Fatalf("no lastGood: %v", pub.topics())
	}

	idle := haFixture(t, "idle.json")
	pub.reset()
	b.Fetch = fetchBody(idle, nil)
	b.Poll(context.Background())
	if len(pub.snapshot()) != 3 {
		t.Fatalf("poll %v", pub.topics())
	}
	pub.reset()
	b.OnConnect()
	if got := pub.topics(); len(got) != 3 || got[0] != TopicDiscovery || got[1] != TopicState || got[2] != TopicAvailability {
		t.Fatalf("reconnect %v", got)
	}
	if string(pub.payloads(TopicAvailability)[0]) != PayloadOnline {
		t.Fatal("want online")
	}

	pub.reset()
	b.Poll(context.Background())
	if len(pub.snapshot()) != 0 {
		t.Fatalf("caches should suppress identical poll, got %v", pub.topics())
	}
}

func TestShutdown(t *testing.T) {
	idle := haFixture(t, "idle.json")
	pub := &fakePub{}
	b := newTestBridge(pub, fetchBody(idle, nil))
	b.Poll(context.Background())
	pub.reset()
	b.Shutdown()
	if got := pub.topics(); len(got) != 1 || got[0] != TopicAvailability {
		t.Fatalf("topics %v", got)
	}
	if string(pub.payloads(TopicAvailability)[0]) != PayloadOffline {
		t.Fatal("want offline")
	}
	requireRetainedQoS1(t, pub.snapshot())
	b.Shutdown()
	if len(pub.payloads(TopicAvailability)) != 2 {
		t.Fatal("Shutdown should publish even if already offline")
	}
}

func TestRepublishAfterFailedPollStaysOffline(t *testing.T) {
	good, err := os.ReadFile("../api/testdata/ha/idle.json")
	if err != nil {
		t.Fatal(err)
	}
	pub := &fakePub{}
	var fetchErr error
	b := newTestBridge(pub, func(context.Context) ([]byte, error) {
		if fetchErr != nil {
			return nil, fetchErr
		}
		return good, nil
	})
	b.Poll(context.Background())
	fetchErr = errors.New("connection refused")
	b.Poll(context.Background())
	for name, republish := range map[string]func(){
		"ha_status": func() { b.OnHAStatus([]byte(PayloadOnline)) },
		"connect":   b.OnConnect,
	} {
		pub.reset()
		republish()
		av := pub.payloads(TopicAvailability)
		if len(av) != 1 || string(av[0]) != PayloadOffline {
			t.Fatalf("%s: availability %q, want one offline", name, av)
		}
		if len(pub.payloads(TopicDiscovery)) != 1 || len(pub.payloads(TopicState)) != 1 {
			t.Fatalf("%s: want discovery and last state republished, got %v", name, pub.topics())
		}
	}
}
