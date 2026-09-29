package kiosk

import (
	"fmt"
	"image"
	"image/color"
	"math"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/image/font"
	"golang.org/x/image/font/gofont/gobold"
	"golang.org/x/image/font/gofont/goregular"
	"golang.org/x/image/font/opentype"
	"golang.org/x/image/math/fixed"
)

// View is the data for one frame. Upcoming is computed when zero-value Found
// is not enough; call Snapshot.View.
type View struct {
	Status   Status
	Stations []Station
	Upcoming Upcoming
	Pressed  Hit
	MenuOpen bool
}

// View builds a frame from a snapshot. The clock and the next-run label use status time.
func (s Snapshot) View(pressed Hit, menuOpen bool) View {
	now, err := s.Status.NowTime()
	if err != nil {
		now = time.Now()
	}
	return View{
		Status:   s.Status,
		Stations: s.Stations,
		Upcoming: NextRun(s.Schedules, s.Stations, now, s.Status.Location()),
		Pressed:  pressed,
		MenuOpen: menuOpen,
	}
}

// Render draws the home screen. animPhase moves the ON pulse; only the fraction matters.
func Render(v View, lay Layout, animPhase float64) *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, ScreenW, ScreenH))
	bg := color.RGBA{16, 18, 24, 255}
	header := color.RGBA{28, 32, 42, 255}
	fillRect(img, img.Bounds(), bg)
	fillRect(img, lay.Header, header)

	title := "Kiosk"
	if v.Status.Paused {
		title = v.Status.PausedLabel
		if title == "" {
			title = "Paused"
		}
	}
	ink := color.RGBA{236, 238, 242, 255}
	drawLeft(img, 22, false, title, lay.Title, ink, 8)
	drawCenter(img, 26, true, clockText(v.Status), lay.Clock, ink)
	if v.Status.RainStrip.Show {
		chip := color.RGBA{22, 96, 78, 255}
		fillRoundRect(img, lay.Rain, 12, chip)
		drawCenter(img, 16, false, rainText(v.Status.RainStrip), lay.Rain, ink)
	}

	n := len(v.Stations)
	if n > len(lay.Tiles) {
		n = len(lay.Tiles)
	}
	pressedTile, tileDown := v.Pressed.Tile()
	for i := 0; i < n; i++ {
		st := v.Stations[i]
		on := v.Status.StationOn(st.ID)
		col := parseColor(st.Color)
		if !on {
			col = shade(col, 48, 100)
		}
		if tileDown && pressedTile == i {
			col = shade(col, 62, 100)
		}
		tile := lay.Tiles[i]
		fillRoundRect(img, tile, 16, col)
		fg := contrast(col)
		titleR := image.Rect(tile.Min.X, tile.Min.Y+4, tile.Max.X, tile.Min.Y+tile.Dy()/2)
		stateR := image.Rect(tile.Min.X, tile.Min.Y+tile.Dy()/2-4, tile.Max.X, tile.Max.Y-26)
		drawCenter(img, 20, false, st.Title, titleR, fg)
		if on {
			drawCenter(img, 18, true, "ON", stateR, fg)
			drawPulse(img, tile, animPhase)
		} else {
			drawCenter(img, 18, false, "idle", stateR, fg)
		}
	}

	stopCol := color.RGBA{204, 36, 40, 255}
	if v.Pressed == HitStop {
		stopCol = shade(stopCol, 58, 100)
	}
	fillRoundRect(img, lay.Stop, 18, stopCol)
	drawCenter(img, 64, true, "STOP", lay.Stop, contrast(stopCol))

	pauseCol := color.RGBA{32, 108, 204, 255}
	pauseLabel := "Pause"
	if v.Status.Paused {
		pauseCol = color.RGBA{24, 142, 86, 255}
		pauseLabel = "Resume"
	}
	if v.Pressed == HitPause {
		pauseCol = shade(pauseCol, 62, 100)
	}
	fillRoundRect(img, lay.Pause, 16, pauseCol)
	drawCenter(img, 32, true, pauseLabel, lay.Pause, contrast(pauseCol))

	nextInk := color.RGBA{214, 218, 226, 255}
	label := "Next: none"
	summary := ""
	if v.Upcoming.Found {
		label = "Next: " + v.Upcoming.Label
		summary = v.Upcoming.Summary
	}
	line := lay.Next
	if summary != "" {
		line.Max.Y = line.Min.Y + 20
	}
	drawLeft(img, 15, false, label, line, nextInk, 1)
	if summary != "" {
		sub := image.Rect(lay.Next.Min.X, line.Max.Y-2, lay.Next.Max.X, lay.Next.Max.Y)
		drawWrapped(img, 13, false, summary, sub, color.RGBA{176, 182, 194, 255})
	}

	menuCol := color.RGBA{54, 58, 72, 255}
	if v.Pressed == HitMenu {
		menuCol = shade(menuCol, 70, 100)
	}
	fillRoundRect(img, lay.Menu, 16, menuCol)
	drawCenter(img, 28, true, "...", lay.Menu, ink)

	if v.MenuOpen {
		fillRoundRect(img, lay.Panel.Inset(-4), 18, color.RGBA{190, 196, 208, 255})
		fillRoundRect(img, lay.Panel, 16, color.RGBA{24, 28, 38, 255})
		head := image.Rect(lay.Panel.Min.X, lay.Panel.Min.Y+8, lay.Panel.Max.X, lay.Panel.Min.Y+56)
		body := image.Rect(lay.Panel.Min.X+12, lay.Panel.Min.Y+64, lay.Panel.Max.X-12, lay.Panel.Max.Y-12)
		drawCenter(img, 26, true, "Menu", head, ink)
		drawWrapped(img, 18, false, "Edits are not in this spike.", body, nextInk)
	}
	return img
}

func clockText(st Status) string {
	t, err := st.NowTime()
	if err != nil {
		return "--:--"
	}
	return t.In(st.Location()).Format("15:04")
}

func rainText(s RainStrip) string {
	h := s.Hours
	if h <= 0 {
		h = 24
	}
	return fmt.Sprintf("Rain %.2f in / %dh", s.Inches, h)
}

func drawPulse(img *image.RGBA, tile image.Rectangle, phase float64) {
	pad := 14
	bar := image.Rect(tile.Min.X+pad, tile.Max.Y-20, tile.Max.X-pad, tile.Max.Y-8)
	if bar.Dx() < 12 || bar.Dy() < 4 {
		return
	}
	fillRoundRect(img, bar, 5, color.RGBA{255, 255, 255, 255})
	fillRoundRect(img, bar.Inset(1), 4, color.RGBA{30, 30, 30, 255})
	w := bar.Dx() / 3
	if w < 6 {
		w = bar.Dx() / 2
	}
	phase = phase - math.Floor(phase)
	if phase < 0 {
		phase += 1
	}
	travel := bar.Dx() - w
	if travel < 1 {
		travel = 1
	}
	x := bar.Min.X + int(phase*float64(travel))
	fillRoundRect(img, image.Rect(x, bar.Min.Y, x+w, bar.Max.Y), 4, color.RGBA{255, 255, 255, 255})
}

func parseColor(s string) color.RGBA {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "red":
		return color.RGBA{196, 48, 48, 255}
	case "yellow":
		return color.RGBA{214, 176, 40, 255}
	case "blue":
		return color.RGBA{46, 110, 210, 255}
	case "green":
		return color.RGBA{40, 160, 80, 255}
	case "orange":
		return color.RGBA{220, 120, 30, 255}
	case "purple":
		return color.RGBA{140, 70, 180, 255}
	case "white":
		return color.RGBA{230, 230, 230, 255}
	case "black":
		return color.RGBA{32, 32, 32, 255}
	}
	if strings.HasPrefix(s, "#") && len(s) == 7 {
		if v, err := strconv.ParseUint(s[1:], 16, 32); err == nil {
			return color.RGBA{uint8(v >> 16), uint8(v >> 8), uint8(v), 255}
		}
	}
	return color.RGBA{70, 80, 100, 255}
}

func shade(c color.RGBA, num, den int) color.RGBA {
	if den <= 0 {
		den = 1
	}
	return color.RGBA{
		R: uint8(int(c.R) * num / den),
		G: uint8(int(c.G) * num / den),
		B: uint8(int(c.B) * num / den),
		A: 255,
	}
}

func contrast(bg color.RGBA) color.RGBA {
	y := (int(bg.R)*299 + int(bg.G)*587 + int(bg.B)*114) / 1000
	if y > 160 {
		return color.RGBA{24, 26, 30, 255}
	}
	return color.RGBA{245, 246, 248, 255}
}

func fillRect(img *image.RGBA, r image.Rectangle, c color.RGBA) {
	fillRoundRect(img, r, 0, c)
}

func fillRoundRect(img *image.RGBA, r image.Rectangle, rad int, c color.RGBA) {
	if img == nil || r.Empty() {
		return
	}
	if rad < 0 {
		rad = 0
	}
	if rad > r.Dx()/2 {
		rad = r.Dx() / 2
	}
	if rad > r.Dy()/2 {
		rad = r.Dy() / 2
	}
	b := img.Bounds()
	y0, y1 := r.Min.Y, r.Max.Y
	if y0 < b.Min.Y {
		y0 = b.Min.Y
	}
	if y1 > b.Max.Y {
		y1 = b.Max.Y
	}
	for y := y0; y < y1; y++ {
		inset := 0
		if dy := y - r.Min.Y; dy < rad {
			inset = cornerInset(rad, dy)
		} else if dy := r.Max.Y - 1 - y; dy < rad {
			inset = cornerInset(rad, dy)
		}
		fillSpan(img, r.Min.X+inset, r.Max.X-inset, y, c)
	}
}

func cornerInset(rad, dy int) int {
	// Pixel centers against a circle seated in the corner.
	yy := float64(dy) + 0.5 - float64(rad)
	limit := float64(rad * rad)
	for x := 0; x < rad; x++ {
		xx := float64(x) + 0.5 - float64(rad)
		if xx*xx+yy*yy <= limit {
			return x
		}
	}
	return rad
}

func fillSpan(img *image.RGBA, x0, x1, y int, c color.RGBA) {
	b := img.Bounds()
	if y < b.Min.Y || y >= b.Max.Y {
		return
	}
	if x0 < b.Min.X {
		x0 = b.Min.X
	}
	if x1 > b.Max.X {
		x1 = b.Max.X
	}
	if x0 >= x1 {
		return
	}
	i := img.PixOffset(x0, y)
	for x := x0; x < x1; x++ {
		img.Pix[i] = c.R
		img.Pix[i+1] = c.G
		img.Pix[i+2] = c.B
		img.Pix[i+3] = 0xFF
		i += 4
	}
}

// ToBGRA32 writes img as little-endian XRGB8888 (bytes B, G, R, X) with the
// given destination stride. X is 0xFF. A short dst stops the copy early.
func ToBGRA32(dst []byte, stride int, img *image.RGBA) {
	if img == nil || stride <= 0 {
		return
	}
	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	rowBytes := w * 4
	if rowBytes > stride {
		rowBytes = stride - stride%4
	}
	for y := 0; y < h; y++ {
		off := y * stride
		if off < 0 || off+rowBytes > len(dst) {
			return
		}
		for x := 0; x < rowBytes/4; x++ {
			si := img.PixOffset(b.Min.X+x, b.Min.Y+y)
			di := off + x*4
			dst[di] = img.Pix[si+2]
			dst[di+1] = img.Pix[si+1]
			dst[di+2] = img.Pix[si]
			dst[di+3] = 0xFF
		}
	}
}

var (
	fontsOnce sync.Once
	fontReg   *opentype.Font
	fontBold  *opentype.Font
	fontsErr  error
	faceMu    sync.Mutex
	faceCache = map[faceKey]font.Face{}
	drawMu    sync.Mutex
)

type faceKey struct {
	size float64
	bold bool
}

func loadFonts() {
	fontsOnce.Do(func() {
		var err error
		fontReg, err = opentype.Parse(goregular.TTF)
		if err != nil {
			fontsErr = err
			return
		}
		fontBold, err = opentype.Parse(gobold.TTF)
		fontsErr = err
	})
}

func textFace(size float64, bold bool) font.Face {
	loadFonts()
	if fontsErr != nil || size <= 0 {
		return nil
	}
	key := faceKey{size, bold}
	faceMu.Lock()
	defer faceMu.Unlock()
	if f, ok := faceCache[key]; ok {
		return f
	}
	src := fontReg
	if bold {
		src = fontBold
	}
	f, err := opentype.NewFace(src, &opentype.FaceOptions{
		Size:    size,
		DPI:     72,
		Hinting: font.HintingNone,
	})
	if err != nil {
		return nil
	}
	faceCache[key] = f
	return f
}

func withFace(size float64, bold bool, fn func(font.Face)) {
	f := textFace(size, bold)
	if f == nil {
		return
	}
	drawMu.Lock()
	defer drawMu.Unlock()
	fn(f)
}

func fitFace(face font.Face, s string, maxW int) string {
	if face == nil || maxW <= 0 || s == "" {
		return ""
	}
	d := font.Drawer{Face: face}
	if d.MeasureString(s).Round() <= maxW {
		return s
	}
	for len(s) > 0 {
		s = s[:len(s)-1]
		t := strings.TrimRight(s, " ") + "..."
		if d.MeasureString(t).Round() <= maxW {
			return t
		}
	}
	return ""
}

func drawCenter(img *image.RGBA, size float64, bold bool, s string, r image.Rectangle, col color.RGBA) {
	withFace(size, bold, func(face font.Face) {
		s = fitFace(face, s, r.Dx()-8)
		if s == "" {
			return
		}
		d := font.Drawer{Face: face}
		w := d.MeasureString(s).Round()
		ascent := face.Metrics().Ascent.Round()
		height := face.Metrics().Height.Round()
		if height < 1 {
			height = ascent
		}
		x := r.Min.X + (r.Dx()-w)/2
		y := r.Min.Y + (r.Dy()-height)/2 + ascent
		dr := &font.Drawer{
			Dst:  img,
			Src:  image.NewUniform(col),
			Face: face,
			Dot:  fixed.P(x, y),
		}
		dr.DrawString(s)
	})
}

func wrapLines(face font.Face, s string, maxW int) []string {
	if face == nil || s == "" || maxW <= 8 {
		return nil
	}
	d := font.Drawer{Face: face}
	fits := func(t string) bool { return d.MeasureString(t).Round() <= maxW }
	if fits(s) {
		return []string{s}
	}
	var units []string
	if strings.Contains(s, ",") {
		parts := strings.Split(s, ",")
		for i, p := range parts {
			p = strings.TrimSpace(p)
			if p == "" {
				continue
			}
			if i != len(parts)-1 {
				p += ","
			}
			units = append(units, p)
		}
	} else {
		units = strings.Fields(s)
	}
	var lines []string
	cur := ""
	flush := func() {
		if cur != "" {
			lines = append(lines, cur)
			cur = ""
		}
	}
	for _, u := range units {
		next := u
		if cur != "" {
			next = cur + " " + u
		}
		if fits(next) {
			cur = next
			continue
		}
		flush()
		if fits(u) {
			cur = u
			continue
		}
		for _, w := range strings.Fields(u) {
			next = w
			if cur != "" {
				next = cur + " " + w
			}
			if fits(next) {
				cur = next
				continue
			}
			flush()
			cur = w
		}
	}
	flush()
	return lines
}

func drawWrapped(img *image.RGBA, size float64, bold bool, s string, r image.Rectangle, col color.RGBA) {
	withFace(size, bold, func(face font.Face) {
		lines := wrapLines(face, s, r.Dx()-4)
		ascent := face.Metrics().Ascent.Round()
		lineH := face.Metrics().Height.Round()
		if lineH < ascent+2 {
			lineH = ascent + 2
		}
		y := r.Min.Y
		for _, line := range lines {
			if y+ascent > r.Max.Y {
				break
			}
			dr := &font.Drawer{
				Dst:  img,
				Src:  image.NewUniform(col),
				Face: face,
				Dot:  fixed.P(r.Min.X+2, y+ascent),
			}
			dr.DrawString(line)
			y += lineH
		}
	})
}

func drawLeft(img *image.RGBA, size float64, bold bool, s string, r image.Rectangle, col color.RGBA, dy int) {
	withFace(size, bold, func(face font.Face) {
		s = fitFace(face, s, r.Dx()-8)
		if s == "" {
			return
		}
		ascent := face.Metrics().Ascent.Round()
		dr := &font.Drawer{
			Dst:  img,
			Src:  image.NewUniform(col),
			Face: face,
			Dot:  fixed.P(r.Min.X+8, r.Min.Y+dy+ascent),
		}
		dr.DrawString(s)
	})
}
