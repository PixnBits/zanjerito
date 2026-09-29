package kiosk

import (
	"encoding/binary"
	"io"
	"os"
	"syscall"
	"time"
	"unsafe"
)

// Touch is one contact edge (or a move) mapped into screen pixels.
// EventTime is the kernel CLOCK_REALTIME timestamp on the SYN_REPORT.
type Touch struct {
	X, Y      int
	Down      bool
	EventTime time.Time
}

// TouchMap turns raw panel coordinates into the 800x480 screen.
// Raw values are clamped to 0..RawW-1 by 0..RawH-1 (default 800x480).
// Order is swap, then flip, then clockwise rotation, then scale.
type TouchMap struct {
	Rotate  int
	SwapXY  bool
	FlipX   bool
	FlipY   bool
	RawW    int
	RawH    int
	ScreenW int
	ScreenH int
}

func (m TouchMap) norm() TouchMap {
	if m.RawW <= 0 {
		m.RawW = ScreenW
	}
	if m.RawH <= 0 {
		m.RawH = ScreenH
	}
	if m.ScreenW <= 0 {
		m.ScreenW = ScreenW
	}
	if m.ScreenH <= 0 {
		m.ScreenH = ScreenH
	}
	return m
}

// Map converts one raw sample. Rotation 90 and 270 scale because the UI stays 800x480.
func (m TouchMap) Map(x, y int) (int, int) {
	m = m.norm()
	rw, rh := m.RawW, m.RawH
	if x < 0 {
		x = 0
	} else if x > rw-1 {
		x = rw - 1
	}
	if y < 0 {
		y = 0
	} else if y > rh-1 {
		y = rh - 1
	}
	if m.SwapXY {
		x, y = y, x
		rw, rh = rh, rw
	}
	if m.FlipX {
		x = rw - 1 - x
	}
	if m.FlipY {
		y = rh - 1 - y
	}
	rot := m.Rotate % 360
	if rot < 0 {
		rot += 360
	}
	nx, ny, nw, nh := x, y, rw, rh
	switch rot {
	case 90:
		nx, ny = y, rw-1-x
		nw, nh = rh, rw
	case 180:
		nx, ny = rw-1-x, rh-1-y
	case 270:
		nx, ny = rh-1-y, x
		nw, nh = rh, rw
	}
	return scaleTo(nx, ny, nw, nh, m.ScreenW, m.ScreenH)
}

func scaleTo(x, y, nw, nh, sw, sh int) (int, int) {
	if nw <= 1 || nh <= 1 || sw <= 1 || sh <= 1 {
		return 0, 0
	}
	if nw != sw {
		x = x * (sw - 1) / (nw - 1)
	}
	if nh != sh {
		y = y * (sh - 1) / (nh - 1)
	}
	if x < 0 {
		x = 0
	} else if x > sw-1 {
		x = sw - 1
	}
	if y < 0 {
		y = 0
	} else if y > sh-1 {
		y = sh - 1
	}
	return x, y
}

// input_event. timeval is syscall.Timeval so the record is 16 bytes on arm
// and 24 bytes on amd64, matching the kernel.
type rawEvent struct {
	Time  syscall.Timeval
	Type  uint16
	Code  uint16
	Value int32
}

const (
	evSyn = 0x00
	evKey = 0x01
	evAbs = 0x03

	synReport = 0
	btnTouch  = 0x14a

	absX            = 0x00
	absY            = 0x01
	absMtSlot       = 0x2f
	absMtPositionX  = 0x35
	absMtPositionY  = 0x36
	absMtTrackingID = 0x39
	maxSlots        = 16
)

func eventSize() int {
	var ev rawEvent
	return int(unsafe.Sizeof(ev))
}

func timeWord() int {
	var ev rawEvent
	return int(unsafe.Sizeof(ev.Time.Sec))
}

func encodeEvent(sec, usec int64, typ, code uint16, val int32) []byte {
	b := make([]byte, eventSize())
	word := timeWord()
	if word == 8 {
		binary.LittleEndian.PutUint64(b[0:8], uint64(sec))
		binary.LittleEndian.PutUint64(b[8:16], uint64(usec))
	} else {
		binary.LittleEndian.PutUint32(b[0:4], uint32(sec))
		binary.LittleEndian.PutUint32(b[4:8], uint32(usec))
	}
	base := word * 2
	binary.LittleEndian.PutUint16(b[base:], typ)
	binary.LittleEndian.PutUint16(b[base+2:], code)
	binary.LittleEndian.PutUint32(b[base+4:], uint32(val))
	return b
}

func decodeEvent(b []byte) (sec, usec int64, typ, code uint16, val int32) {
	word := timeWord()
	if word == 8 {
		sec = int64(binary.LittleEndian.Uint64(b[0:8]))
		usec = int64(binary.LittleEndian.Uint64(b[8:16]))
	} else {
		sec = int64(int32(binary.LittleEndian.Uint32(b[0:4])))
		usec = int64(int32(binary.LittleEndian.Uint32(b[4:8])))
	}
	base := word * 2
	typ = binary.LittleEndian.Uint16(b[base:])
	code = binary.LittleEndian.Uint16(b[base+2:])
	val = int32(binary.LittleEndian.Uint32(b[base+4:]))
	return sec, usec, typ, code, val
}

type slot struct {
	id    int32
	x, y  int
	press bool
	lift  bool
	move  bool
}

type devState struct {
	inited   bool
	slot     int
	slots    [maxSlots]slot
	seenMT   bool
	absX     int
	absY     int
	absMove  bool
	btnDown  bool
	btnPress bool
	btnLift  bool
}

func (s *devState) init() {
	if s.inited {
		return
	}
	for i := range s.slots {
		s.slots[i].id = -1
	}
	s.inited = true
}

func (s *devState) apply(typ, code uint16, val int32) {
	switch typ {
	case evAbs:
		switch code {
		case absMtSlot:
			s.seenMT = true
			if val >= 0 && int(val) < len(s.slots) {
				s.slot = int(val)
			}
		case absMtTrackingID:
			s.seenMT = true
			sl := &s.slots[s.slot]
			if val < 0 {
				if sl.id >= 0 {
					sl.lift = true
				}
				sl.id = -1
			} else {
				if sl.id < 0 {
					sl.press = true
				}
				sl.id = val
			}
		case absMtPositionX:
			s.seenMT = true
			sl := &s.slots[s.slot]
			sl.x = int(val)
			sl.move = true
		case absMtPositionY:
			s.seenMT = true
			sl := &s.slots[s.slot]
			sl.y = int(val)
			sl.move = true
		case absX:
			s.absX = int(val)
			s.absMove = true
		case absY:
			s.absY = int(val)
			s.absMove = true
		}
	case evKey:
		if code == btnTouch {
			down := val != 0
			if down && !s.btnDown {
				s.btnPress = true
			}
			if !down && s.btnDown {
				s.btnLift = true
			}
			s.btnDown = down
		}
	}
}

func (s *devState) commit(ts time.Time, m TouchMap) []Touch {
	var out []Touch
	if s.seenMT {
		for i := range s.slots {
			sl := &s.slots[i]
			if sl.press {
				x, y := m.Map(sl.x, sl.y)
				out = append(out, Touch{X: x, Y: y, Down: true, EventTime: ts})
				sl.press = false
				sl.move = false
			} else if sl.move && sl.id >= 0 {
				x, y := m.Map(sl.x, sl.y)
				out = append(out, Touch{X: x, Y: y, Down: true, EventTime: ts})
				sl.move = false
			}
			if sl.lift {
				x, y := m.Map(sl.x, sl.y)
				out = append(out, Touch{X: x, Y: y, Down: false, EventTime: ts})
				sl.lift = false
				sl.move = false
			}
		}
	} else {
		x, y := m.Map(s.absX, s.absY)
		if s.btnPress {
			out = append(out, Touch{X: x, Y: y, Down: true, EventTime: ts})
		} else if s.absMove && s.btnDown {
			out = append(out, Touch{X: x, Y: y, Down: true, EventTime: ts})
		}
		if s.btnLift {
			out = append(out, Touch{X: x, Y: y, Down: false, EventTime: ts})
		}
	}
	s.btnPress = false
	s.btnLift = false
	s.absMove = false
	return out
}

// Reader decodes a Linux evdev stream. It does not grab the device.
type Reader struct {
	r   io.Reader
	c   io.Closer
	buf []byte
	m   TouchMap
	st  devState
	q   []Touch
}

// NewReader parses events from r. r is not closed.
func NewReader(r io.Reader, m TouchMap) *Reader {
	rd := &Reader{r: r, m: m.norm(), buf: make([]byte, eventSize())}
	rd.st.init()
	return rd
}

// OpenTouch opens path read-only. The device is not grabbed.
func OpenTouch(path string, m TouchMap) (*Reader, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	rd := NewReader(f, m)
	rd.c = f
	return rd, nil
}

// Close closes the device opened by OpenTouch.
func (rd *Reader) Close() error {
	if rd == nil || rd.c == nil {
		return nil
	}
	err := rd.c.Close()
	rd.c = nil
	return err
}

// ReadTouch blocks until the next touch sample or the stream ends.
func (rd *Reader) ReadTouch() (Touch, error) {
	rd.st.init()
	for {
		if len(rd.q) > 0 {
			t := rd.q[0]
			rd.q = rd.q[1:]
			return t, nil
		}
		if _, err := io.ReadFull(rd.r, rd.buf); err != nil {
			return Touch{}, err
		}
		sec, usec, typ, code, val := decodeEvent(rd.buf)
		if typ == evSyn && code == synReport {
			ts := time.Unix(sec, usec*1000).UTC()
			rd.q = append(rd.q, rd.st.commit(ts, rd.m)...)
			continue
		}
		rd.st.apply(typ, code, val)
	}
}
