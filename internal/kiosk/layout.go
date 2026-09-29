package kiosk

import (
	"fmt"
	"image"
)

const (
	// ScreenW and ScreenH are the panel size in pixels (~267 px/inch).
	ScreenW = 800
	ScreenH = 480
)

// Hit is a touch target. Tile indexes use HitTile.
type Hit int

const (
	HitNone Hit = iota
	HitStop
	HitPause
	HitMenu
	hitTileBase
)

// HitTile is the target for station tile i (0-based, row-major).
func HitTile(i int) Hit {
	if i < 0 {
		return HitNone
	}
	return hitTileBase + Hit(i)
}

// Tile reports the station index when h is a tile.
func (h Hit) Tile() (int, bool) {
	if h < hitTileBase {
		return 0, false
	}
	return int(h - hitTileBase), true
}

func (h Hit) String() string {
	switch h {
	case HitNone:
		return "None"
	case HitStop:
		return "Stop"
	case HitPause:
		return "Pause"
	case HitMenu:
		return "Menu"
	default:
		if i, ok := h.Tile(); ok {
			return fmt.Sprintf("Tile(%d)", i)
		}
		return fmt.Sprintf("Hit(%d)", int(h))
	}
}

// Layout is the 800x480 home screen. Hit rects are the touch targets and may
// extend a few pixels past the drawn rects. Hits do not overlap.
type Layout struct {
	Header   image.Rectangle
	Title    image.Rectangle
	Clock    image.Rectangle
	Rain     image.Rectangle
	Tiles    []image.Rectangle
	TileHits []image.Rectangle
	Stop     image.Rectangle
	StopHit  image.Rectangle
	Pause    image.Rectangle
	PauseHit image.Rectangle
	Next     image.Rectangle
	Menu     image.Rectangle
	MenuHit  image.Rectangle
	Panel    image.Rectangle
}

// NewLayout places up to 6 station tiles. Extra stations are not shown.
func NewLayout(nStations int) Layout {
	if nStations < 0 {
		nStations = 0
	}
	if nStations > 6 {
		nStations = 6
	}
	const (
		margin  = 8
		headerH = 44
		gap     = 10
		pad     = 3 // hit inflate into the gutter
		rightX  = 504
		stopH   = 220
		pauseH  = 128
		menuS   = 96
	)
	top := headerH + margin
	rightR := ScreenW - margin

	var l Layout
	l.Header = image.Rect(0, 0, ScreenW, headerH)
	l.Title = image.Rect(margin, 0, 340, headerH)
	l.Rain = image.Rect(348, 6, 620, headerH-6)
	l.Clock = image.Rect(628, 0, rightR, headerH)

	stopCell := image.Rect(rightX, top, rightR, top+stopH)
	pauseCell := image.Rect(rightX, stopCell.Max.Y+gap, rightR, stopCell.Max.Y+gap+pauseH)
	l.Stop = stopCell
	l.Pause = pauseCell
	l.Next = image.Rect(rightX, pauseCell.Max.Y+gap, rightR, ScreenH-margin)
	l.StopHit = inflate(stopCell, pad)
	l.PauseHit = inflate(pauseCell, pad)

	menuCell := image.Rect(margin, ScreenH-margin-menuS, margin+menuS, ScreenH-margin)
	l.Menu = menuCell
	l.MenuHit = inflate(menuCell, pad)

	// Two columns, three rows, above the menu corner.
	grid := image.Rect(margin, top, rightX-gap, menuCell.Min.Y-gap)
	const cols, rows = 2, 3
	tileW := (grid.Dx() - gap*(cols-1)) / cols
	tileH := (grid.Dy() - gap*(rows-1)) / rows
	l.Tiles = make([]image.Rectangle, nStations)
	l.TileHits = make([]image.Rectangle, nStations)
	for i := 0; i < nStations; i++ {
		c := i % cols
		r := i / cols
		x := grid.Min.X + c*(tileW+gap)
		y := grid.Min.Y + r*(tileH+gap)
		cell := image.Rect(x, y, x+tileW, y+tileH)
		l.Tiles[i] = cell
		l.TileHits[i] = inflate(cell, pad)
	}
	// Sits over the tile grid only, so it does not cover STOP.
	l.Panel = image.Rect(148, 92, 496, 240)
	return l
}

func inflate(r image.Rectangle, n int) image.Rectangle {
	grown := image.Rect(r.Min.X-n, r.Min.Y-n, r.Max.X+n, r.Max.Y+n)
	return grown.Intersect(image.Rect(0, 0, ScreenW, ScreenH))
}

// HitTest returns the target under a screen pixel.
func (l Layout) HitTest(x, y int) Hit {
	p := image.Pt(x, y)
	if p.In(l.MenuHit) {
		return HitMenu
	}
	if p.In(l.StopHit) {
		return HitStop
	}
	if p.In(l.PauseHit) {
		return HitPause
	}
	for i, r := range l.TileHits {
		if p.In(r) {
			return HitTile(i)
		}
	}
	return HitNone
}

// Rects lists every layout rectangle, drawn and hit, for invariant checks.
func (l Layout) Rects() []image.Rectangle {
	out := []image.Rectangle{
		l.Header, l.Title, l.Clock, l.Rain,
		l.Stop, l.StopHit, l.Pause, l.PauseHit,
		l.Next, l.Menu, l.MenuHit, l.Panel,
	}
	out = append(out, l.Tiles...)
	out = append(out, l.TileHits...)
	return out
}

// Hits lists touch targets. They do not overlap.
func (l Layout) Hits() []image.Rectangle {
	out := []image.Rectangle{l.StopHit, l.PauseHit, l.MenuHit}
	out = append(out, l.TileHits...)
	return out
}
