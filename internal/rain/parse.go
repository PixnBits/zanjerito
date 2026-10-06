package rain

import (
	"bufio"
	"fmt"
	"io"
	"regexp"
	"sort"
	"strconv"
	"time"
)

// fcdmcLine matches one ALERT row: date, time, cumulative inches, increment.
// Extra columns are ignored. Headers and markup do not match.
var fcdmcLine = regexp.MustCompile(`^\s*(\d{2}/\d{2}/\d{4})\s+(\d{2}:\d{2}:\d{2})\s+(-?[\d.]+)\s+(-?[\d.]+)`)

const phoenixTZ = "America/Phoenix"

// ParseFCDMC reads an FCDMC ALERT precipitation page. Rows in the body are
// newest-first; the result is oldest-first. loc nil uses America/Phoenix.
// Negative increments are kept as-is. Unparseable lines are skipped.
// Zero parsed rows returns an error containing "no samples".
func ParseFCDMC(r io.Reader, loc *time.Location) ([]Sample, error) {
	if loc == nil {
		var err error
		loc, err = time.LoadLocation(phoenixTZ)
		if err != nil {
			loc = time.FixedZone("MST", -7*3600)
		}
	}
	sc := bufio.NewScanner(r)
	// A full page is small; raise the default so a long preamble cannot split a row.
	sc.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	var samples []Sample
	for sc.Scan() {
		m := fcdmcLine.FindStringSubmatch(sc.Text())
		if m == nil {
			continue
		}
		when, err := time.ParseInLocation("01/02/2006 15:04:05", m[1]+" "+m[2], loc)
		if err != nil {
			continue
		}
		inc, err := strconv.ParseFloat(m[4], 64)
		if err != nil {
			continue
		}
		samples = append(samples, Sample{Time: when, Inches: inc})
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}
	if len(samples) == 0 {
		return nil, fmt.Errorf("no samples")
	}
	sort.SliceStable(samples, func(i, j int) bool {
		return samples[i].Time.Before(samples[j].Time)
	})
	return samples, nil
}
