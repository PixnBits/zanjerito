package rain

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"strings"
	"time"
)

const (
	// DefaultBody is the ALERT showdata form. {gauge} is replaced with the
	// URL-escaped gauge id. NM is the sample count.
	DefaultBody = "ID1={gauge}&ST=rain&NM=200"
	// DefaultTimeout bounds the HTTP client and is paired with the request context.
	DefaultTimeout = 15 * time.Second
)

// FCDMC fetches one gauge from the Flood Control District of Maricopa County
// ALERT showdata endpoint. GET is not accepted by that service; Method defaults
// to POST.
type FCDMC struct {
	URL     string
	Method  string
	Body    string
	GaugeID string
	Loc     *time.Location
	HTTP    *http.Client
}

// NewFCDMC builds a client from cfg. Timeout defaults to 15s. The client
// timeout and the Fetch context both apply.
func NewFCDMC(cfg Config, loc *time.Location) *FCDMC {
	cfg = cfg.normalized()
	if loc == nil {
		var err error
		loc, err = time.LoadLocation(phoenixTZ)
		if err != nil {
			loc = time.FixedZone("MST", -7*3600)
		}
	}
	return &FCDMC{
		URL:     cfg.URL,
		Method:  cfg.Method,
		Body:    cfg.Body,
		GaugeID: cfg.GaugeID,
		Loc:     loc,
		HTTP:    &http.Client{Timeout: cfg.Timeout},
	}
}

// Fetch posts the form template and parses incremental samples.
// Errors do not include the gauge id or the request URL.
func (f *FCDMC) Fetch(ctx context.Context) ([]Sample, error) {
	if f == nil {
		return nil, fmt.Errorf("fetch failed")
	}
	method := f.Method
	if method == "" {
		method = http.MethodPost
	}
	bodyTmpl := f.Body
	if bodyTmpl == "" {
		bodyTmpl = DefaultBody
	}
	form := strings.ReplaceAll(bodyTmpl, "{gauge}", url.QueryEscape(f.GaugeID))
	req, err := http.NewRequestWithContext(ctx, method, f.URL, strings.NewReader(form))
	if err != nil {
		return nil, fmt.Errorf("fetch failed")
	}
	if method == http.MethodPost || method == http.MethodPut || method == http.MethodPatch {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}
	client := f.HTTP
	if client == nil {
		client = &http.Client{Timeout: DefaultTimeout}
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%s", redactErr(err, f.GaugeID))
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	samples, err := ParseFCDMC(io.LimitReader(resp.Body, 2<<20), f.Loc)
	if err != nil {
		return nil, fmt.Errorf("%s", redact(err.Error(), f.GaugeID))
	}
	return samples, nil
}

var (
	// Stop before a quote so Post "http://host/path": EOF keeps its closing quote.
	urlPattern      = regexp.MustCompile(`https?://[^\s"'<>]+`)
	ipv4PortPattern = regexp.MustCompile(`(?:\d{1,3}\.){3}\d{1,3}:\d+`)
	ipv6PortPattern = regexp.MustCompile(`\[[0-9a-fA-F:]*\]:\d+`)
)

// redactErr rebuilds a transport error without the request URL or gauge id.
// url.Error formats as Op "URL": inner; copying that string and substituting
// the URL swallows the closing quote, so the URL is dropped before formatting.
func redactErr(err error, gaugeID string) string {
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
			inner = redact(uerr.Err.Error(), gaugeID)
		}
		return fmt.Sprintf("%s %q: %s", op, "<redacted>", inner)
	}
	return redact(err.Error(), gaugeID)
}

// redact strips URLs, host:port pairs, and the gauge id.
// The URL match stops at a quote so a wrapped Post "…": err stays balanced.
func redact(msg, gaugeID string) string {
	msg = urlPattern.ReplaceAllString(msg, "<redacted>")
	if gaugeID != "" {
		msg = strings.ReplaceAll(msg, gaugeID, "<redacted>")
		if esc := url.QueryEscape(gaugeID); esc != "" && esc != gaugeID {
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
