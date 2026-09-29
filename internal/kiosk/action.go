package kiosk

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"strings"
	"time"
)

// DefaultPause is the duration used when the Pause button has no picker.
const DefaultPause = 24 * time.Hour

// Actions talks to the daemon. The CLI uses ReadOnlyActions unless -allow-writes is set.
type Actions interface {
	Stop() error
	Pause(d time.Duration) error
	Resume() error
}

// HTTPActions POSTs stop, pause, and resume. Not for use against a live daemon in the spike.
type HTTPActions struct {
	BaseURL string
	HTTP    *http.Client
}

// NewHTTPActions posts to baseURL. Redirects are not followed, so a 3xx is an error
// instead of a second POST to another host.
func NewHTTPActions(baseURL string) HTTPActions {
	return HTTPActions{
		BaseURL: strings.TrimRight(baseURL, "/"),
		HTTP: &http.Client{
			Timeout: ClientTimeout,
			CheckRedirect: func(*http.Request, []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
	}
}

type pauseReq struct {
	DurationSec int    `json:"duration_sec"`
	Reason      string `json:"reason"`
}

func (a HTTPActions) client() *http.Client {
	if a.HTTP != nil {
		return a.HTTP
	}
	return &http.Client{Timeout: ClientTimeout}
}

func (a HTTPActions) endpoint(path string) string {
	return strings.TrimRight(a.BaseURL, "/") + path
}

func (a HTTPActions) post(path string, body any) error {
	var rdr io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			return err
		}
		rdr = bytes.NewReader(b)
	}
	ctx, cancel := context.WithTimeout(context.Background(), ClientTimeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, a.endpoint(path), rdr)
	if err != nil {
		return err
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := a.client().Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1<<20))
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("POST %s: %s", path, resp.Status)
	}
	return nil
}

// Stop POSTs /api/run/cancel.
func (a HTTPActions) Stop() error {
	return a.post("/api/run/cancel", nil)
}

// Pause POSTs /api/pause. d <= 0 uses DefaultPause.
func (a HTTPActions) Pause(d time.Duration) error {
	if d <= 0 {
		d = DefaultPause
	}
	sec := int(d / time.Second)
	if sec < 1 {
		sec = 1
	}
	return a.post("/api/pause", pauseReq{DurationSec: sec, Reason: "kiosk"})
}

// Resume POSTs /api/pause/resume.
func (a HTTPActions) Resume() error {
	return a.post("/api/pause/resume", nil)
}

// ReadOnlyActions logs the POST it would make and does not touch the network.
type ReadOnlyActions struct {
	Logf func(string, ...any)
}

func (a ReadOnlyActions) log(path string) {
	msg := "readonly: would POST " + path
	if a.Logf != nil {
		a.Logf("%s", msg)
		return
	}
	log.Print(msg)
}

// Stop logs and returns nil.
func (a ReadOnlyActions) Stop() error {
	a.log("/api/run/cancel")
	return nil
}

// Pause logs and returns nil.
func (a ReadOnlyActions) Pause(time.Duration) error {
	a.log("/api/pause")
	return nil
}

// Resume logs and returns nil.
func (a ReadOnlyActions) Resume() error {
	a.log("/api/pause/resume")
	return nil
}

// Dispatch runs the action for a release on Stop or Pause. Tiles and Menu do nothing.
func Dispatch(a Actions, h Hit, paused bool, pauseFor time.Duration) error {
	if a == nil {
		return nil
	}
	switch h {
	case HitStop:
		return a.Stop()
	case HitPause:
		if paused {
			return a.Resume()
		}
		if pauseFor <= 0 {
			pauseFor = DefaultPause
		}
		return a.Pause(pauseFor)
	default:
		return nil
	}
}

// Release is the touch-up policy. Actions run only when the finger lands on the
// same Stop or Pause target it pressed, and only while the menu is closed.
// A menu press toggles the placeholder. An open menu swallows the release.
func Release(a Actions, pressed, up Hit, menuOpen, paused bool, pauseFor time.Duration) (bool, error) {
	if menuOpen {
		return false, nil
	}
	if pressed == HitMenu && up == HitMenu {
		return true, nil
	}
	if up != pressed {
		return false, nil
	}
	return false, Dispatch(a, up, paused, pauseFor)
}

var (
	_ Actions = HTTPActions{}
	_ Actions = ReadOnlyActions{}
)
