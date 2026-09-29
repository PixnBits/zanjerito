package kiosk

import (
	"bytes"
	"errors"
	"io"
	"os"
	"runtime"
	"testing"
	"time"
	"unsafe"
)

func TestInputEventLayout(t *testing.T) {
	var ev rawEvent
	word := int(unsafe.Sizeof(ev.Time.Sec))
	if int(unsafe.Sizeof(ev)) != word*2+8 {
		t.Fatalf("input_event size %d", unsafe.Sizeof(ev))
	}
	if unsafe.Offsetof(ev.Type) != uintptr(word*2) ||
		unsafe.Offsetof(ev.Code) != uintptr(word*2+2) ||
		unsafe.Offsetof(ev.Value) != uintptr(word*2+4) {
		t.Fatalf("offsets type %d code %d value %d", unsafe.Offsetof(ev.Type), unsafe.Offsetof(ev.Code), unsafe.Offsetof(ev.Value))
	}
	switch runtime.GOARCH {
	case "amd64", "arm64":
		if word != 8 || unsafe.Sizeof(ev) != 24 {
			t.Fatalf("word %d size %d", word, unsafe.Sizeof(ev))
		}
	case "arm", "386":
		if word != 4 || unsafe.Sizeof(ev) != 16 {
			t.Fatalf("word %d size %d", word, unsafe.Sizeof(ev))
		}
	}
}

func TestParseMultitouch(t *testing.T) {
	var buf []byte
	add := func(typ, code uint16, val int32, sec, usec int64) {
		buf = append(buf, encodeEvent(sec, usec, typ, code, val)...)
	}
	add(evAbs, absMtSlot, 0, 1700000000, 1000)
	add(evAbs, absMtTrackingID, 42, 1700000000, 1000)
	add(evAbs, absMtPositionX, 100, 1700000000, 1000)
	add(evAbs, absMtPositionY, 200, 1700000000, 1000)
	add(evSyn, synReport, 0, 1700000000, 5000)
	add(evAbs, absMtTrackingID, -1, 1700000001, 0)
	add(evSyn, synReport, 0, 1700000001, 250)

	rd := NewReader(bytes.NewReader(buf), TouchMap{})
	down, err := rd.ReadTouch()
	if err != nil {
		t.Fatal(err)
	}
	if !down.Down || down.X != 100 || down.Y != 200 {
		t.Fatalf("down %+v", down)
	}
	if !down.EventTime.Equal(time.Unix(1700000000, 5000*1000)) {
		t.Fatal(down.EventTime)
	}
	up, err := rd.ReadTouch()
	if err != nil {
		t.Fatal(err)
	}
	if up.Down || up.X != 100 || up.Y != 200 {
		t.Fatalf("up %+v", up)
	}
	_, err = rd.ReadTouch()
	if !errors.Is(err, io.EOF) {
		t.Fatal(err)
	}

	rot := NewReader(bytes.NewReader(buf), TouchMap{Rotate: 180})
	turned, err := rot.ReadTouch()
	if err != nil {
		t.Fatal(err)
	}
	if turned.X != 699 || turned.Y != 279 || !turned.Down {
		t.Fatalf("rotated %+v", turned)
	}
}

func TestParseSingleTouch(t *testing.T) {
	var buf []byte
	add := func(typ, code uint16, val int32) {
		buf = append(buf, encodeEvent(50, 0, typ, code, val)...)
	}
	add(evAbs, absX, 15)
	add(evAbs, absY, 25)
	add(evKey, btnTouch, 1)
	add(evSyn, synReport, 0)
	add(evKey, btnTouch, 0)
	add(evSyn, synReport, 0)
	rd := NewReader(bytes.NewReader(buf), TouchMap{})
	down, err := rd.ReadTouch()
	if err != nil {
		t.Fatal(err)
	}
	if !down.Down || down.X != 15 || down.Y != 25 {
		t.Fatalf("%+v", down)
	}
	up, err := rd.ReadTouch()
	if err != nil {
		t.Fatal(err)
	}
	if up.Down || up.X != 15 || up.Y != 25 {
		t.Fatalf("%+v", up)
	}
}

func TestTouchMap(t *testing.T) {
	id := TouchMap{}
	if x, y := id.Map(0, 0); x != 0 || y != 0 {
		t.Fatalf("origin %d %d", x, y)
	}
	if x, y := id.Map(799, 479); x != 799 || y != 479 {
		t.Fatalf("corner %d %d", x, y)
	}
	if x, y := id.Map(100, 200); x != 100 || y != 200 {
		t.Fatalf("id %d %d", x, y)
	}
	if x, y := id.Map(-5, 900); x != 0 || y != 479 {
		t.Fatalf("clamp %d %d", x, y)
	}
	if x, y := (TouchMap{FlipX: true}).Map(0, 10); x != 799 || y != 10 {
		t.Fatalf("flipx %d %d", x, y)
	}
	if x, y := (TouchMap{FlipY: true}).Map(10, 0); x != 10 || y != 479 {
		t.Fatalf("flipy %d %d", x, y)
	}
	if x, y := (TouchMap{Rotate: 180}).Map(100, 200); x != 699 || y != 279 {
		t.Fatalf("rot180 %d %d", x, y)
	}
	rot90 := TouchMap{Rotate: 90}
	corners := [][4]int{
		{0, 0, 0, 479},
		{799, 0, 0, 0},
		{799, 479, 799, 0},
		{0, 479, 799, 479},
	}
	for _, c := range corners {
		x, y := rot90.Map(c[0], c[1])
		if x != c[2] || y != c[3] {
			t.Fatalf("90 %d,%d -> %d,%d want %d,%d", c[0], c[1], x, y, c[2], c[3])
		}
	}
	if x, y := (TouchMap{Rotate: 270}).Map(0, 0); x != 799 || y != 0 {
		t.Fatalf("270 %d %d", x, y)
	}
	if x, y := (TouchMap{SwapXY: true}).Map(0, 479); x != 799 || y != 0 {
		t.Fatalf("swap %d %d", x, y)
	}
}

func TestShortRead(t *testing.T) {
	rd := NewReader(bytes.NewReader([]byte{1, 2, 3}), TouchMap{})
	_, err := rd.ReadTouch()
	if !errors.Is(err, io.ErrUnexpectedEOF) && !errors.Is(err, io.EOF) {
		t.Fatal(err)
	}
}

func TestTouchOpenDoesNotGrab(t *testing.T) {
	b, err := os.ReadFile("input_linux.go")
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Contains(b, []byte("EVIOCGRAB")) || bytes.Contains(b, []byte("0x40044590")) {
		t.Fatal("device grab is not allowed")
	}
}
