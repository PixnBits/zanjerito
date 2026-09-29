package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/PixnBits/zanjerito/internal/kiosk"
)

type cliOpt struct {
	api           *string
	fixture       *string
	fixtureStatus *string
	sink          *string
	png           *string
	touch         *string
	rotate        *int
	swapXY        *bool
	flipX         *bool
	flipY         *bool
	anim          *bool
	duration      *time.Duration
	stats         *bool
	allowWrites   *bool
}

func defineFlags(fs *flag.FlagSet) *cliOpt {
	return &cliOpt{
		api:           fs.String("api", "http://127.0.0.1:8080", "daemon base URL"),
		fixture:       fs.String("fixture", "", "directory of JSON fixtures; skips HTTP reads"),
		fixtureStatus: fs.String("fixture-status", "idle", "fixture status: idle, running, or paused"),
		sink:          fs.String("sink", "mem", "output sink: mem, png, or fb (fb only if explicitly selected)"),
		png:           fs.String("png", "kiosk.png", "path written by -sink png"),
		touch:         fs.String("touch", "", "evdev device path, for example /dev/input/eventN"),
		rotate:        fs.Int("rotate", 0, "touch rotation: 0, 90, 180, or 270"),
		swapXY:        fs.Bool("swapxy", false, "swap touch X/Y before flips and rotation"),
		flipX:         fs.Bool("flipx", false, "flip touch X"),
		flipY:         fs.Bool("flipy", false, "flip touch Y"),
		anim:          fs.Bool("anim", false, "redraw at 30 fps with a moving pulse; otherwise redraw only when state or touch changes"),
		duration:      fs.Duration("duration", 0, "exit after this duration (0 waits for a signal)"),
		stats:         fs.Bool("stats", false, "print JSON timing stats on exit"),
		allowWrites:   fs.Bool("allow-writes", false, "POST stop/pause/resume to -api. Not for use against a live daemon in the spike"),
	}
}

func chooseActions(allow bool, api string) kiosk.Actions {
	if !allow {
		return kiosk.ReadOnlyActions{}
	}
	return kiosk.NewHTTPActions(api)
}

func main() {
	mainStart := time.Now()
	log.SetOutput(os.Stderr)
	log.SetFlags(log.LstdFlags)
	opt := defineFlags(flag.CommandLine)
	flag.Parse()
	rep, err := run(opt, mainStart)
	if *opt.stats {
		if e := json.NewEncoder(os.Stdout).Encode(rep); e != nil && err == nil {
			err = e
		}
	}
	if err != nil {
		log.Print(err)
		os.Exit(1)
	}
}

func run(opt *cliOpt, mainStart time.Time) (rep runReport, err error) {
	acc := &frameStats{mainStart: mainStart}
	defer func() { rep = acc.report() }()

	rot := *opt.rotate % 360
	if rot < 0 {
		rot += 360
	}
	switch rot {
	case 0, 90, 180, 270:
	default:
		err = fmt.Errorf("rotate %d: want 0, 90, 180, or 270", *opt.rotate)
		return
	}

	ctx, cancel := context.WithCancel(context.Background())
	var sink kiosk.Sink
	var touch *kiosk.Reader
	defer func() {
		cancel()
		if touch != nil {
			_ = touch.Close()
		}
		if sink != nil {
			_ = sink.Close()
		}
	}()

	var client *kiosk.Client
	var snap kiosk.Snapshot
	if *opt.fixture != "" {
		snap, err = kiosk.LoadFixture(*opt.fixture, *opt.fixtureStatus)
	} else {
		client = kiosk.NewClient(*opt.api)
		loadCtx, loadCancel := context.WithTimeout(ctx, kiosk.ClientTimeout)
		snap, err = client.Load(loadCtx)
		loadCancel()
	}
	if err != nil {
		return
	}

	sink, err = kiosk.OpenSink(*opt.sink, *opt.png)
	if err != nil {
		return
	}
	actions := chooseActions(*opt.allowWrites, *opt.api)
	if *opt.allowWrites {
		log.Print("allow-writes: POSTing to the daemon; not for use against a live daemon in the spike")
	}

	if *opt.touch != "" {
		touch, err = kiosk.OpenTouch(*opt.touch, kiosk.TouchMap{
			Rotate: rot,
			SwapXY: *opt.swapXY,
			FlipX:  *opt.flipX,
			FlipY:  *opt.flipY,
		})
		if err != nil {
			return
		}
	}

	layout := kiosk.NewLayout(len(snap.Stations))
	pressed := kiosk.HitNone
	menuOpen := false
	phase := func() float64 { return 0 }
	if *opt.anim {
		phase = func() float64 { return time.Since(mainStart).Seconds() }
	}
	render := func(ph float64, touchAt []time.Time) error {
		t0 := time.Now()
		img := kiosk.Render(snap.View(pressed, menuOpen), layout, ph)
		if werr := sink.Write(img); werr != nil {
			return werr
		}
		done := time.Now()
		acc.frames = append(acc.frames, done.Sub(t0))
		if acc.first.IsZero() {
			acc.first = done
		}
		for _, at := range touchAt {
			d := done.Sub(at)
			if d < 0 {
				d = 0
			}
			acc.lats = append(acc.lats, d)
		}
		return nil
	}
	if err = render(phase(), nil); err != nil {
		return
	}

	var snapC chan kiosk.Snapshot
	if client != nil {
		snapC = make(chan kiosk.Snapshot, 4)
		go func() {
			_ = client.Poll(ctx, kiosk.DefaultPoll(), snap, func(s kiosk.Snapshot) {
				select {
				case snapC <- s:
				case <-ctx.Done():
				}
			})
		}()
	}
	var touchC chan kiosk.Touch
	if touch != nil {
		touchC = make(chan kiosk.Touch, 32)
		go func() {
			defer close(touchC)
			for {
				t, rerr := touch.ReadTouch()
				if rerr != nil {
					if ctx.Err() == nil && !errors.Is(rerr, io.EOF) {
						log.Printf("touch: %v", rerr)
					}
					return
				}
				select {
				case touchC <- t:
				case <-ctx.Done():
					return
				}
			}
		}()
	}

	var animC <-chan time.Time
	if *opt.anim {
		tk := time.NewTicker(time.Second / 30)
		defer tk.Stop()
		animC = tk.C
	}
	var durC <-chan time.Time
	if *opt.duration > 0 {
		durC = time.After(*opt.duration)
	}
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(sig)

	for {
		select {
		case <-durC:
			return
		case <-sig:
			return
		case s := <-snapC:
			snap = s
			layout = kiosk.NewLayout(len(snap.Stations))
			if i, ok := pressed.Tile(); ok && i >= len(layout.Tiles) {
				pressed = kiosk.HitNone
			}
			if !*opt.anim {
				if err = render(0, nil); err != nil {
					return
				}
			}
		case t, ok := <-touchC:
			if !ok {
				touchC = nil
				continue
			}
			acc.touches++
			h := layout.HitTest(t.X, t.Y)
			if t.Down {
				pressed = h
			} else {
				var rerr error
				menuOpen, rerr = kiosk.Release(actions, pressed, h, menuOpen, snap.Status.Paused, kiosk.DefaultPause)
				pressed = kiosk.HitNone
				if rerr != nil {
					log.Printf("action: %v", rerr)
				}
			}
			if err = render(phase(), []time.Time{t.EventTime}); err != nil {
				return
			}
		case <-animC:
			if err = render(phase(), nil); err != nil {
				return
			}
		}
	}
}
