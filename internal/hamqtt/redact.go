package hamqtt

import (
	"fmt"
	"net/url"
	"strings"
)

// RedactURL replaces URL userinfo (user and password) with REDACTED.
// A string that is not a URL with a scheme and host becomes "<invalid url>"
// so a password in a bad value is not echoed.
func RedactURL(raw string) string {
	u, err := url.Parse(raw)
	if err != nil || u.Scheme == "" || u.Host == "" {
		return "<invalid url>"
	}
	if u.User == nil {
		return raw
	}
	u.User = url.User("REDACTED")
	return u.String()
}

// Redact replaces every occurrence of each non-empty secret with REDACTED.
// Longer secrets are applied first so a shorter secret cannot split a longer one.
func Redact(s string, secrets ...string) string {
	secs := make([]string, 0, len(secrets))
	for _, sec := range secrets {
		if sec != "" {
			secs = append(secs, sec)
		}
	}
	for i := 1; i < len(secs); i++ {
		j := i
		for j > 0 && len(secs[j]) > len(secs[j-1]) {
			secs[j], secs[j-1] = secs[j-1], secs[j]
			j--
		}
	}
	for _, sec := range secs {
		s = strings.ReplaceAll(s, sec, "REDACTED")
	}
	return s
}

// URLPasswords returns the broker URL password and, when it differs, the
// encoded userinfo form. Both can show up in client errors.
func URLPasswords(raw string) []string {
	u, err := url.Parse(raw)
	if err != nil || u.User == nil {
		return nil
	}
	pass, ok := u.User.Password()
	if !ok || pass == "" {
		return nil
	}
	out := []string{pass}
	if enc := encodedUserinfoPassword(u.User); enc != "" && enc != pass {
		out = append(out, enc)
	}
	return out
}

func encodedUserinfoPassword(user *url.Userinfo) string {
	s := user.String()
	i := strings.Index(s, ":")
	if i < 0 || i+1 >= len(s) {
		return ""
	}
	return s[i+1:]
}

// StartupLine is the process startup log line. It never includes cfg.Pass
// or a URL userinfo password. When the broker URL carries userinfo and
// MQTT_USER is empty, the user field is "(from url)".
func StartupLine(cfg Config, version string) string {
	return fmt.Sprintf("zanjerito-mqtt %s broker=%s client_id=%s user=%s ha=%s poll=%s refresh=%s",
		version,
		RedactURL(cfg.URL),
		cfg.ClientID,
		userField(cfg),
		RedactURL(cfg.HAURL),
		cfg.PollInterval,
		cfg.RefreshInterval,
	)
}

func userField(cfg Config) string {
	if cfg.User != "" {
		return cfg.User
	}
	if urlHasUserinfo(cfg.URL) {
		return "(from url)"
	}
	return "(none)"
}

func urlHasUserinfo(raw string) bool {
	u, err := url.Parse(raw)
	if err != nil || u.Scheme == "" || u.Host == "" {
		rest := raw
		if i := strings.Index(raw, "://"); i >= 0 {
			rest = raw[i+3:]
		}
		at := strings.Index(rest, "@")
		return at > 0
	}
	return u.User != nil
}
