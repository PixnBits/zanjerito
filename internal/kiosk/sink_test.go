package kiosk

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"os"
	"testing"
	"unsafe"
)

func TestToBGRA32(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 2, 1))
	img.SetRGBA(0, 0, color.RGBA{R: 1, G: 2, B: 3, A: 4})
	img.SetRGBA(1, 0, color.RGBA{R: 9, G: 8, B: 7, A: 6})
	dst := make([]byte, 16)
	for i := range dst {
		dst[i] = 0xAB
	}
	ToBGRA32(dst, 12, img)
	want := []byte{3, 2, 1, 0xFF, 7, 8, 9, 0xFF}
	if !bytes.Equal(dst[:8], want) {
		t.Fatalf("%v", dst[:8])
	}
	for _, b := range dst[8:12] {
		if b != 0xAB {
			t.Fatalf("stride gap overwritten %v", dst)
		}
	}
}

func TestMemSinkConverts(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 2, 2))
	img.SetRGBA(0, 0, color.RGBA{R: 10, G: 20, B: 30, A: 255})
	var m MemSink
	if err := m.Write(img); err != nil {
		t.Fatal(err)
	}
	if m.Frames != 1 || len(m.buf) != 16 {
		t.Fatalf("frames %d len %d", m.Frames, len(m.buf))
	}
	if m.buf[0] != 30 || m.buf[1] != 20 || m.buf[2] != 10 || m.buf[3] != 0xFF {
		t.Fatalf("%v", m.buf[:4])
	}
	if err := m.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestBlitRespectsStride(t *testing.T) {
	src := []byte{9, 8, 7, 6, 5, 4, 3, 2}
	mem := make([]byte, 32)
	for i := range mem {
		mem[i] = 0x11
	}
	blitBGRA(mem, 16, 1, 1, 4, src, 8, 2, 1)
	// Row 1, x offset 1 pixel: bytes 16+4 .. 16+4+8.
	if !bytes.Equal(mem[20:28], src) {
		t.Fatalf("%v", mem)
	}
	if mem[16] != 0x11 || mem[28] != 0x11 || mem[0] != 0x11 {
		t.Fatalf("neighbor overwritten %v", mem)
	}
}

func TestPNGSinkRoundTrip(t *testing.T) {
	snap, err := LoadFixture("testdata", "idle")
	if err != nil {
		t.Fatal(err)
	}
	lay := NewLayout(len(snap.Stations))
	img := Render(snap.View(HitNone, false), lay, 0)
	path := t.TempDir() + "/frame.png"
	s, err := OpenSink("png", path)
	if err != nil {
		t.Fatal(err)
	}
	if err := s.Write(img); err != nil {
		t.Fatal(err)
	}
	if err := s.Close(); err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	got, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	if got.Bounds().Dx() != ScreenW || got.Bounds().Dy() != ScreenH {
		t.Fatal(got.Bounds())
	}
}

func TestOpenSinkMem(t *testing.T) {
	s, err := OpenSink("mem", "")
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := s.(*MemSink); !ok {
		t.Fatalf("%T", s)
	}
	if _, err := OpenSink("nope", ""); err == nil {
		t.Fatal("expected error")
	}
	if _, err := OpenSink("", ""); err != nil {
		t.Fatal(err)
	}
}

func TestFBStructLayout(t *testing.T) {
	var v fbVarScreenInfo
	if unsafe.Sizeof(v) != 160 {
		t.Fatal(unsafe.Sizeof(v))
	}
	if unsafe.Offsetof(v.BitsPerPixel) != 24 || unsafe.Offsetof(v.Xoffset) != 16 {
		t.Fatalf("bpp %d xoff %d", unsafe.Offsetof(v.BitsPerPixel), unsafe.Offsetof(v.Xoffset))
	}
	smem, line := fixFieldOffsets()
	if bits := unsafe.Sizeof(uintptr(0)); bits == 8 {
		if smem != 24 || line != 48 || fixInfoSize() != 80 {
			t.Fatalf("64-bit fix %d %d %d", smem, line, fixInfoSize())
		}
	} else if bits == 4 {
		if smem != 20 || line != 44 || fixInfoSize() != 68 {
			t.Fatalf("32-bit fix %d %d %d", smem, line, fixInfoSize())
		}
	}
}
