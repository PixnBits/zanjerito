package soil

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// Source fetches daily reference ET. Fetch must honor ctx.
type Source interface {
	Fetch(ctx context.Context, start time.Time, extraDays int) ([]DayET, error)
}

// AZMET GETs one station's daily observations.
type AZMET struct {
	BaseURL   string
	StationID string
	HTTP      *http.Client
}

// NewAZMET builds a client from cfg. Timeout defaults to 15s. The client
// timeout and the Fetch context both apply.
func NewAZMET(cfg Config) *AZMET {
	cfg = cfg.normalized()
	return &AZMET{
		BaseURL:   cfg.URL,
		StationID: cfg.Station,
		HTTP:      &http.Client{Timeout: cfg.Timeout},
	}
}

// Fetch GETs extraDays+1 daily rows starting at start's calendar date.
// Errors do not include the station id or the request URL.
func (a *AZMET) Fetch(ctx context.Context, start time.Time, extraDays int) ([]DayET, error) {
	if a == nil {
		return nil, fmt.Errorf("fetch failed")
	}
	if extraDays < 0 {
		extraDays = 0
	}
	base := strings.TrimRight(strings.TrimSpace(a.BaseURL), "/")
	if base == "" {
		base = DefaultURL
	}
	day := start.Format("2006-01-02")
	interval := fmt.Sprintf("P%dD", extraDays)
	rawURL := base + "/" + a.StationID + "/" + day + "T00:00/" + interval
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return nil, fmt.Errorf("fetch failed")
	}
	client := a.HTTP
	if client == nil {
		client = &http.Client{Timeout: DefaultTimeout}
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%s", redactErr(err, a.StationID))
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusTooManyRequests {
		_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("rate-limited")
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	days, err := parseAZMET(io.LimitReader(resp.Body, 2<<20))
	if err != nil {
		return nil, fmt.Errorf("%s", redact(err.Error(), a.StationID))
	}
	return days, nil
}

type azmetDoc struct {
	Data []map[string]json.RawMessage `json:"data"`
}

func parseAZMET(r io.Reader) ([]DayET, error) {
	b, err := io.ReadAll(r)
	if err != nil {
		return nil, err
	}
	var doc azmetDoc
	if err := json.Unmarshal(b, &doc); err != nil {
		return nil, fmt.Errorf("bad JSON")
	}
	if doc.Data == nil {
		return nil, fmt.Errorf("no observations")
	}
	var days []DayET
	seen := map[string]int{}
	for _, row := range doc.Data {
		date, ok := parseDate(rawString(row, "datetime"))
		if !ok {
			continue
		}
		inches, ok := rowETInches(row)
		if !ok {
			continue
		}
		review := strings.TrimSpace(rawString(row, "meta_needs_review"))
		d := DayET{Date: date, ETInches: inches, NeedsReview: review != "" && review != "0"}
		if i, dup := seen[date]; dup {
			days[i] = d
			continue
		}
		seen[date] = len(days)
		days = append(days, d)
	}
	if len(days) == 0 {
		return nil, fmt.Errorf("no observations")
	}
	return days, nil
}

func rowETInches(row map[string]json.RawMessage) (float64, bool) {
	if mm, ok := rawFloat(row, "eto_azmet"); ok {
		return mm / mmPerInch, finite(mm / mmPerInch)
	}
	if in, ok := rawFloat(row, "eto_azmet_in"); ok {
		return in, finite(in)
	}
	return 0, false
}

func finite(v float64) bool {
	return !math.IsNaN(v) && !math.IsInf(v, 0)
}

func parseDate(s string) (string, bool) {
	s = strings.TrimSpace(s)
	if len(s) < 10 {
		return "", false
	}
	day := s[:10]
	if _, err := time.Parse("2006-01-02", day); err != nil {
		return "", false
	}
	return day, true
}

func rawString(m map[string]json.RawMessage, key string) string {
	b, ok := m[key]
	if !ok || len(b) == 0 || string(b) == "null" {
		return ""
	}
	var s string
	if err := json.Unmarshal(b, &s); err == nil {
		return strings.TrimSpace(s)
	}
	return strings.TrimSpace(string(b))
}

func rawFloat(m map[string]json.RawMessage, key string) (float64, bool) {
	s := rawString(m, key)
	if s == "" {
		return 0, false
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return 0, false
	}
	if !finite(v) {
		return 0, false
	}
	return v, true
}

var (
	urlPattern      = regexp.MustCompile(`https?://[^\s"'<>]+`)
	ipv4PortPattern = regexp.MustCompile(`(?:\d{1,3}\.){3}\d{1,3}:\d+`)
	ipv6PortPattern = regexp.MustCompile(`\[[0-9a-fA-F:]*\]:\d+`)
)

func redactErr(err error, stationID string) string {
	if err == nil {
		return "fetch failed"
	}
	var uerr *url.Error
	if errors.As(err, &uerr) {
		op := uerr.Op
		if strings.TrimSpace(op) == "" {
			op = "GET"
		}
		inner := "fetch failed"
		if uerr.Err != nil {
			inner = redact(uerr.Err.Error(), stationID)
		}
		return fmt.Sprintf("%s %q: %s", op, "<redacted>", inner)
	}
	return redact(err.Error(), stationID)
}

func redact(msg, stationID string) string {
	msg = urlPattern.ReplaceAllString(msg, "<redacted>")
	if stationID != "" {
		msg = strings.ReplaceAll(msg, stationID, "<redacted>")
		if esc := url.QueryEscape(stationID); esc != "" && esc != stationID {
			msg = strings.ReplaceAll(msg, esc, "<redacted>")
		}
	}
	msg = ipv4PortPattern.ReplaceAllString(msg, "<redacted>")
	msg = ipv6PortPattern.ReplaceAllString(msg, "<redacted>")
	msg = strings.TrimSpace(msg)
	if msg == "" {
		return "fetch failed"
	}
	return msg
}
