package kiosk

import (
	"image"
	"testing"
)

func TestLayoutInvariants(t *testing.T) {
	screen := image.Rect(0, 0, ScreenW, ScreenH)
	for n := 0; n <= 8; n++ {
		l := NewLayout(n)
		wantTiles := n
		if wantTiles > 6 {
			wantTiles = 6
		}
		if len(l.Tiles) != wantTiles || len(l.TileHits) != wantTiles {
			t.Fatalf("n=%d tiles %d", n, len(l.Tiles))
		}
		for _, r := range l.Rects() {
			if r.Empty() || !r.In(screen) {
				t.Fatalf("n=%d rect %v outside", n, r)
			}
		}
		hits := l.Hits()
		for i, h := range hits {
			if h.Dx() < 80 || h.Dy() < 80 {
				t.Fatalf("n=%d hit %v smaller than 80", n, h)
			}
			for j := i + 1; j < len(hits); j++ {
				if h.Overlaps(hits[j]) {
					t.Fatalf("n=%d overlap %v %v", n, h, hits[j])
				}
			}
		}
		if l.Stop.Dy() < 190 || l.Pause.Dy() < 100 {
			t.Fatalf("drawn heights stop %d pause %d", l.Stop.Dy(), l.Pause.Dy())
		}
		if l.Stop.Dy()*2 < l.Pause.Dy()*3 {
			t.Fatalf("stop %d < 1.5x pause %d", l.Stop.Dy(), l.Pause.Dy())
		}
		if l.StopHit.Dy() < 160 || l.StopHit.Dy()*2 < l.PauseHit.Dy()*3 {
			t.Fatalf("hit heights stop %d pause %d", l.StopHit.Dy(), l.PauseHit.Dy())
		}
		if l.Stop.Dx() < 200 || l.Stop.Dx() != l.Pause.Dx() || l.Stop.Dx() != l.Next.Dx() {
			t.Fatalf("column width stop %v pause %v next %v", l.Stop, l.Pause, l.Next)
		}
		if l.Stop.Min.X != l.Pause.Min.X || l.Stop.Max.X != l.Pause.Max.X || l.Stop.Max.X != l.Next.Max.X {
			t.Fatal("stop is not full right-column width")
		}
		if l.Menu.Dx() < 96 || l.Menu.Dy() < 96 || l.MenuHit.Dx() < 96 || l.MenuHit.Dy() < 96 {
			t.Fatalf("menu %v hit %v", l.Menu, l.MenuHit)
		}
		pairs := [][2]image.Rectangle{{l.Stop, l.StopHit}, {l.Pause, l.PauseHit}, {l.Menu, l.MenuHit}}
		for i := range l.Tiles {
			pairs = append(pairs, [2]image.Rectangle{l.Tiles[i], l.TileHits[i]})
			if l.Tiles[i].Dx() < 80 || l.Tiles[i].Dy() < 80 {
				t.Fatalf("tile %v", l.Tiles[i])
			}
		}
		for _, p := range pairs {
			if !p[0].In(p[1]) || area(p[1]) <= area(p[0]) {
				t.Fatalf("hit %v not larger than drawn %v", p[1], p[0])
			}
		}
		stopA := area(l.Stop)
		if stopA <= area(l.Pause) || stopA <= area(l.PauseHit) || stopA <= area(l.Next) || stopA <= area(l.Header) {
			t.Fatalf("stop area %d is not dominant", stopA)
		}
		for _, r := range []image.Rectangle{l.Pause, l.Next, l.Clock, l.Menu, l.Panel} {
			if area(r) >= stopA {
				t.Fatalf("rect %v area %d >= stop %d", r, area(r), stopA)
			}
		}
		for _, tile := range l.Tiles {
			if area(tile) >= stopA {
				t.Fatalf("tile %v >= stop", tile)
			}
		}
	}
}

func TestHitTestCenters(t *testing.T) {
	l := NewLayout(4)
	if len(l.TileHits) != 4 {
		t.Fatal(len(l.TileHits))
	}
	if !(l.TileHits[0].Min.X < l.TileHits[1].Min.X) || !(l.TileHits[0].Min.Y < l.TileHits[2].Min.Y) {
		t.Fatal("tiles are not row-major")
	}
	for i, r := range l.TileHits {
		x, y := mid(r)
		if got := l.HitTest(x, y); got != HitTile(i) {
			t.Fatalf("hit tile %d: %s", i, got)
		}
		x, y = mid(l.Tiles[i])
		if got := l.HitTest(x, y); got != HitTile(i) {
			t.Fatalf("drawn tile %d: %s", i, got)
		}
	}
	checks := []struct {
		r    image.Rectangle
		want Hit
	}{
		{l.Stop, HitStop},
		{l.StopHit, HitStop},
		{l.Pause, HitPause},
		{l.Menu, HitMenu},
	}
	for _, c := range checks {
		x, y := mid(c.r)
		if got := l.HitTest(x, y); got != c.want {
			t.Fatalf("%v: %s", c.r, got)
		}
	}
	// Padding just outside the drawn STOP still belongs to STOP.
	x := l.StopHit.Min.X
	_, y := mid(l.Stop)
	if image.Pt(x, y).In(l.Stop) {
		t.Fatal("expected padding outside the drawn stop")
	}
	if got := l.HitTest(x, y); got != HitStop {
		t.Fatalf("padding hit %s", got)
	}
	a, b := l.TileHits[0], l.TileHits[1]
	if a.Max.X >= b.Min.X {
		t.Fatal("no gutter")
	}
	gx := (a.Max.X + b.Min.X) / 2
	gy := (a.Min.Y + a.Max.Y) / 2
	if got := l.HitTest(gx, gy); got != HitNone {
		t.Fatalf("gutter %s", got)
	}
	if l.HitTest(0, 0) != HitNone || l.HitTest(-1, 10) != HitNone || l.HitTest(ScreenW, 200) != HitNone {
		t.Fatal("outside should miss")
	}
	if NewLayout(0).HitTest(mid2(NewLayout(0).Stop)) != HitStop {
		t.Fatal("empty grid still has stop")
	}
}

func mid(r image.Rectangle) (int, int) {
	return (r.Min.X + r.Max.X) / 2, (r.Min.Y + r.Max.Y) / 2
}

func mid2(r image.Rectangle) (int, int) { return mid(r) }

func area(r image.Rectangle) int { return r.Dx() * r.Dy() }
