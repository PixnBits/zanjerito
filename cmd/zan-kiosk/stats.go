package main

import (
	"fmt"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Linux sched_clock reports starttime in USER_HZ ticks. USER_HZ is 100.
const userHZ = 100

type runReport struct {
	ProcessStartToFirstFrameMs float64 `json:"process_start_to_first_frame_ms"`
	MainStartToFirstFrameMs    float64 `json:"main_start_to_first_frame_ms"`
	Frames                     int     `json:"frames"`
	AvgFrameMs                 float64 `json:"avg_frame_ms"`
	P95FrameMs                 float64 `json:"p95_frame_ms"`
	TouchEvents                int     `json:"touch_events"`
	AvgTouchToFrameMs          float64 `json:"avg_touch_to_frame_ms"`
	P95TouchToFrameMs          float64 `json:"p95_touch_to_frame_ms"`
	MaxTouchToFrameMs          float64 `json:"max_touch_to_frame_ms"`
	VmRSSKB                    int64   `json:"vm_rss_kb"`
	VmHWMKB                    int64   `json:"vm_hwm_kb"`
	CPUUserSec                 float64 `json:"cpu_user_sec"`
	CPUSysSec                  float64 `json:"cpu_sys_sec"`
	CPUPercent                 float64 `json:"cpu_percent"`
	WallSec                    float64 `json:"wall_sec"`
}

type frameStats struct {
	mainStart time.Time
	first     time.Time
	frames    []time.Duration
	lats      []time.Duration
	touches   int
}

func (a frameStats) report() runReport {
	rep := runReport{
		Frames:            len(a.frames),
		TouchEvents:       a.touches,
		AvgFrameMs:        ms(avgDur(a.frames)),
		P95FrameMs:        ms(p95Dur(a.frames)),
		AvgTouchToFrameMs: ms(avgDur(a.lats)),
		P95TouchToFrameMs: ms(p95Dur(a.lats)),
		MaxTouchToFrameMs: ms(maxDur(a.lats)),
	}
	if !a.first.IsZero() {
		rep.MainStartToFirstFrameMs = ms(a.first.Sub(a.mainStart))
	}
	end := time.Now()
	wall := end.Sub(a.mainStart).Seconds()
	if ps, err := processStart(); err == nil {
		wall = end.Sub(ps).Seconds()
		if !a.first.IsZero() {
			rep.ProcessStartToFirstFrameMs = ms(a.first.Sub(ps))
		}
	}
	if wall < 0 {
		wall = 0
	}
	rep.WallSec = wall
	if user, sys, err := procCPU(); err == nil {
		rep.CPUUserSec = user
		rep.CPUSysSec = sys
		if wall > 0 {
			rep.CPUPercent = (user + sys) / wall * 100
		}
	}
	if rss, hwm, err := procMem(); err == nil {
		rep.VmRSSKB = rss
		rep.VmHWMKB = hwm
	}
	return rep
}

func ms(d time.Duration) float64 {
	return float64(d.Microseconds()) / 1000
}

func avgDur(ds []time.Duration) time.Duration {
	if len(ds) == 0 {
		return 0
	}
	var s time.Duration
	for _, d := range ds {
		s += d
	}
	return s / time.Duration(len(ds))
}

func maxDur(ds []time.Duration) time.Duration {
	var m time.Duration
	for _, d := range ds {
		if d > m {
			m = d
		}
	}
	return m
}

func p95Dur(ds []time.Duration) time.Duration {
	if len(ds) == 0 {
		return 0
	}
	cp := append([]time.Duration(nil), ds...)
	sort.Slice(cp, func(i, j int) bool { return cp[i] < cp[j] })
	i := int(math.Ceil(0.95*float64(len(cp)))) - 1
	if i < 0 {
		i = 0
	}
	if i >= len(cp) {
		i = len(cp) - 1
	}
	return cp[i]
}

func processStart() (time.Time, error) {
	b, err := os.ReadFile("/proc/stat")
	if err != nil {
		return time.Time{}, err
	}
	boot, err := parseBootUnix(string(b))
	if err != nil {
		return time.Time{}, err
	}
	st, err := os.ReadFile("/proc/self/stat")
	if err != nil {
		return time.Time{}, err
	}
	_, _, ticks, err := parseProcStat(string(st))
	if err != nil {
		return time.Time{}, err
	}
	sec := int64(ticks / userHZ)
	nsec := int64(ticks%userHZ) * (int64(time.Second) / userHZ)
	return time.Unix(boot+sec, nsec), nil
}

func procCPU() (userSec, sysSec float64, err error) {
	b, err := os.ReadFile("/proc/self/stat")
	if err != nil {
		return 0, 0, err
	}
	user, sys, _, err := parseProcStat(string(b))
	if err != nil {
		return 0, 0, err
	}
	return float64(user) / userHZ, float64(sys) / userHZ, nil
}

func procMem() (rss, hwm int64, err error) {
	b, err := os.ReadFile("/proc/self/status")
	if err != nil {
		return 0, 0, err
	}
	text := string(b)
	rss, err = parseStatusKB(text, "VmRSS")
	if err != nil {
		return 0, 0, err
	}
	hwm, err = parseStatusKB(text, "VmHWM")
	return rss, hwm, err
}

func parseBootUnix(text string) (int64, error) {
	for _, line := range strings.Split(text, "\n") {
		if strings.HasPrefix(line, "btime ") {
			return strconv.ParseInt(strings.TrimSpace(strings.TrimPrefix(line, "btime ")), 10, 64)
		}
	}
	return 0, fmt.Errorf("btime missing")
}

// parseProcStat reads utime, stime, and starttime. comm may contain spaces.
func parseProcStat(s string) (user, sys, start uint64, err error) {
	i := strings.LastIndex(s, ")")
	if i < 0 || i+2 > len(s) {
		return 0, 0, 0, fmt.Errorf("proc stat: no comm")
	}
	fields := strings.Fields(s[i+2:])
	if len(fields) < 20 {
		return 0, 0, 0, fmt.Errorf("proc stat: short")
	}
	user, err = strconv.ParseUint(fields[11], 10, 64)
	if err != nil {
		return 0, 0, 0, err
	}
	sys, err = strconv.ParseUint(fields[12], 10, 64)
	if err != nil {
		return 0, 0, 0, err
	}
	start, err = strconv.ParseUint(fields[19], 10, 64)
	if err != nil {
		return 0, 0, 0, err
	}
	return user, sys, start, nil
}

func parseStatusKB(text, key string) (int64, error) {
	prefix := key + ":"
	for _, line := range strings.Split(text, "\n") {
		if !strings.HasPrefix(line, prefix) {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 2 {
			return 0, fmt.Errorf("bad %s", key)
		}
		return strconv.ParseInt(fields[1], 10, 64)
	}
	return 0, fmt.Errorf("%s missing", key)
}
