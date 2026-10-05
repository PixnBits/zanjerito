package main

import (
	"context"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"path/filepath"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/store"
)

type fixedClock struct{ t time.Time }

func (f fixedClock) Now() time.Time { return f.t }

func frontCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{
			{ID: "front-west", Title: "Front West", BCM: 5},
			{ID: "front-north", Title: "Front North", BCM: 6},
			{ID: "front-south", Title: "Front South", BCM: 19},
		},
	}
}

func TestSchedulesRunWithoutNetwork(t *testing.T) {
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	cfg := frontCfg()
	drv := gpio.NewFake()
	eng, err := engine.New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer eng.Close()

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID:       "probe",
		Enabled:  true,
		Weekdays: []string{"mon"},
		Start:    "06:00",
		Steps:    []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}

	r, err := schedule.NewRunner(eng, path)
	if err != nil {
		t.Fatal(err)
	}
	clk := fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)}
	r.Now = clk
	r.Log = log.New(io.Discard, "", 0)

	src := &rain.Fake{}
	src.Set(nil, errors.New("network is unreachable"))
	p := &rain.Poller{
		Source: src,
		Eng:    eng,
		Path:   path,
		Cfg: rain.Config{
			Enabled: true, GaugeID: "TEST-GAUGE",
			TriggerInches: 0.25, WindowHours: 24,
			DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
			PollSeconds: 2,
		},
		Now: func() time.Time { return clk.Now() },
		Log: log.New(io.Discard, "", 0),
	}
	p.SetSaveFuncForTest(func(string, store.PauseState) error { return nil })

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	var attempts atomic.Int32
	listen := func(network, addr string) (net.Listener, error) {
		attempts.Add(1)
		return nil, bindErr(syscall.EADDRNOTAVAIL)
	}
	httpSrv := &http.Server{Addr: "192.0.2.10:8080", Handler: okHandler(), ReadHeaderTimeout: 10 * time.Second}
	apiDone := startAPI(ctx, httpSrv, listen, frozenRetry(5*time.Millisecond), func(string, ...any) {})

	go p.Loop(ctx)

	tickDone := make(chan struct{})
	go func() {
		defer close(tickDone)
		_ = r.Tick(ctx)
	}()

	waitFor(t, 2*time.Second, "schedule run did not start without network", func() bool {
		st := eng.Status()
		on := gpio.StateForTest(drv)["front-west"] == gpio.On
		return st.Phase != engine.PhaseIdle || on
	})
	rst := p.Status()
	if !rst.Unavailable && rst.LastError == "" {
		waitFor(t, time.Second, "rain did not report fetch error", func() bool {
			rst = p.Status()
			return rst.Unavailable || rst.LastError != ""
		})
		rst = p.Status()
	}
	if !rst.Unavailable || rst.LastError == "" {
		t.Fatalf("rain should be unavailable with error, got %+v", rst)
	}
	if attempts.Load() == 0 {
		t.Fatal("serveAPI should still be retrying (attempts > 0)")
	}

	if err := eng.Stop(); err != nil {
		t.Fatalf("Stop: %v", err)
	}
	cancel()
	waitFor(t, 500*time.Millisecond, "Tick did not return after Stop", func() bool {
		select {
		case <-tickDone:
			return true
		default:
			return false
		}
	})
	waitFor(t, 500*time.Millisecond, "serveAPI did not exit after cancel", func() bool {
		select {
		case <-apiDone:
			return true
		default:
			return false
		}
	})
}
