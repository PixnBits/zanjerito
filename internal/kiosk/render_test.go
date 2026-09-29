package kiosk

import (
	"bytes"
	"image"
	"testing"
)

func TestRenderSmoke(t *testing.T) {
	running, err := LoadFixture("testdata", "running")
	if err != nil {
		t.Fatal(err)
	}
	lay := NewLayout(len(running.Stations))
	view := running.View(HitNone, false)
	img := Render(view, lay, 0)
	if img.Bounds() != image.Rect(0, 0, ScreenW, ScreenH) {
		t.Fatal(img.Bounds())
	}
	if uniform(img) {
		t.Fatal("blank frame")
	}
	band := image.Rect(lay.Stop.Min.X+24, lay.Stop.Min.Y+8, lay.Stop.Max.X-24, lay.Stop.Min.Y+28)
	if !redDominant(img, band) {
		t.Fatal("STOP band is not red-dominant")
	}
	pressed := Render(running.View(HitStop, false), lay, 0)
	if meanR(pressed, band) >= meanR(img, band) {
		t.Fatal("pressed STOP is not darker")
	}
	moved := Render(view, lay, 0.55)
	if bytes.Equal(img.Pix, moved.Pix) {
		t.Fatal("ON pulse did not move")
	}
	again := Render(view, lay, 0)
	if !bytes.Equal(img.Pix, again.Pix) {
		t.Fatal("render is not deterministic")
	}

	idle, err := LoadFixture("testdata", "idle")
	if err != nil {
		t.Fatal(err)
	}
	idleView := idle.View(HitNone, false)
	a := Render(idleView, lay, 0)
	b := Render(idleView, lay, 0.55)
	if !bytes.Equal(a.Pix, b.Pix) {
		t.Fatal("idle frame changed with anim phase")
	}
	if bytes.Equal(a.Pix, img.Pix) {
		t.Fatal("running frame matches idle")
	}
	paused, err := LoadFixture("testdata", "paused")
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(a.Pix, Render(paused.View(HitNone, false), lay, 0).Pix) {
		t.Fatal("paused frame matches idle")
	}
	if bytes.Equal(img.Pix, Render(running.View(HitNone, true), lay, 0).Pix) {
		t.Fatal("menu did not draw")
	}
	running.Status.RainStrip.Show = false
	if bytes.Equal(img.Pix, Render(running.View(HitNone, false), lay, 0).Pix) {
		t.Fatal("rain chip still drawn when hidden")
	}
}

func uniform(img *image.RGBA) bool {
	if len(img.Pix) < 4 {
		return true
	}
	r, g, b := img.Pix[0], img.Pix[1], img.Pix[2]
	for i := 0; i < len(img.Pix); i += 4 {
		if img.Pix[i] != r || img.Pix[i+1] != g || img.Pix[i+2] != b {
			return false
		}
	}
	return true
}

func redDominant(img *image.RGBA, r image.Rectangle) bool {
	var rs, gs, bs, n int
	for y := r.Min.Y; y < r.Max.Y; y++ {
		for x := r.Min.X; x < r.Max.X; x++ {
			c := img.RGBAAt(x, y)
			rs += int(c.R)
			gs += int(c.G)
			bs += int(c.B)
			n++
		}
	}
	return n > 0 && rs > gs && rs > bs && rs/n > 100
}

func meanR(img *image.RGBA, r image.Rectangle) int {
	var s, n int
	for y := r.Min.Y; y < r.Max.Y; y++ {
		for x := r.Min.X; x < r.Max.X; x++ {
			s += int(img.RGBAAt(x, y).R)
			n++
		}
	}
	if n == 0 {
		return 0
	}
	return s / n
}
