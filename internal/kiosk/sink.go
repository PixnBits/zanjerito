package kiosk

import (
	"encoding/binary"
	"fmt"
	"image"
	"image/png"
	"os"
	"runtime"
	"strings"
	"syscall"
	"unsafe"
)

// Sink receives a finished frame.
type Sink interface {
	Write(img *image.RGBA) error
	Close() error
}

// MemSink converts each frame to BGRA and discards it, so a run can time
// render+convert without a display. The buffer is reused and not written out.
type MemSink struct {
	Frames int
	Bytes  int
	buf    []byte
}

// Write converts img and keeps the bytes only until the next frame.
func (m *MemSink) Write(img *image.RGBA) error {
	if img == nil {
		return fmt.Errorf("mem sink: nil image")
	}
	b := img.Bounds()
	stride := b.Dx() * 4
	n := stride * b.Dy()
	if cap(m.buf) < n {
		m.buf = make([]byte, n)
	}
	m.buf = m.buf[:n]
	ToBGRA32(m.buf, stride, img)
	m.Frames++
	m.Bytes += n
	return nil
}

// Close is a no-op.
func (m *MemSink) Close() error { return nil }

// PNGSink overwrites Path on each frame.
type PNGSink struct {
	Path string
}

// Write encodes img as PNG.
func (p *PNGSink) Write(img *image.RGBA) error {
	if p.Path == "" {
		return fmt.Errorf("png sink: empty path")
	}
	f, err := os.Create(p.Path)
	if err != nil {
		return err
	}
	err = png.Encode(f, img)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	return err
}

// Close is a no-op. The file is closed in Write.
func (p *PNGSink) Close() error { return nil }

// OpenSink selects mem (default), png, or fb. fb is used only when kind is "fb".
func OpenSink(kind, pngPath string) (Sink, error) {
	switch strings.ToLower(strings.TrimSpace(kind)) {
	case "", "mem":
		return &MemSink{}, nil
	case "png":
		if pngPath == "" {
			pngPath = "kiosk.png"
		}
		return &PNGSink{Path: pngPath}, nil
	case "fb":
		return OpenFB("/dev/fb0")
	default:
		return nil, fmt.Errorf("sink %q: want mem, png, or fb", kind)
	}
}

const (
	fbIOGetVScreenInfo = 0x4600
	fbIOGetFScreenInfo = 0x4602
)

// fbVarScreenInfo matches linux/fb.h fb_var_screeninfo (all __u32, 160 bytes).
type fbVarScreenInfo struct {
	Xres, Yres, XresVirtual, YresVirtual uint32
	Xoffset, Yoffset                     uint32
	BitsPerPixel, Grayscale              uint32
	Red, Green, Blue, Transp             fbBitfield
	Nonstd, Activate                     uint32
	Height, Width                        uint32
	AccelFlags                           uint32
	Pixclock                             uint32
	LeftMargin, RightMargin              uint32
	UpperMargin, LowerMargin             uint32
	HsyncLen, VsyncLen                   uint32
	Sync, Vmode                          uint32
	Rotate                               uint32
	Colorspace                           uint32
	Reserved                             [4]uint32
}

type fbBitfield struct {
	Offset, Length, MsbRight uint32
}

// fixFieldOffsets are smem_len and line_length in fb_fix_screeninfo.
// unsigned long is 8 bytes on 64-bit and 4 on armv7, which shifts the fields.
func fixFieldOffsets() (smemLen, lineLength int) {
	if unsafe.Sizeof(uintptr(0)) == 8 {
		return 24, 48
	}
	return 20, 44
}

func fixInfoSize() int {
	if unsafe.Sizeof(uintptr(0)) == 8 {
		return 80
	}
	return 68
}

// FBSink maps /dev/fb0 and copies BGRA rows at the hardware stride.
type FBSink struct {
	f    *os.File
	mem  []byte
	line int
	xres int
	yres int
	xoff int
	yoff int
	bpp  int
	pack []byte
}

// OpenFB maps the framebuffer at path. Call it only when the fb sink is selected.
func OpenFB(path string) (*FBSink, error) {
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		return nil, err
	}
	s, err := mapFB(f)
	if err != nil {
		_ = f.Close()
		return nil, err
	}
	return s, nil
}

func mapFB(f *os.File) (*FBSink, error) {
	var v fbVarScreenInfo
	_, _, errno := syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), fbIOGetVScreenInfo, uintptr(unsafe.Pointer(&v)))
	runtime.KeepAlive(&v)
	if errno != 0 {
		return nil, fmt.Errorf("fb: var screeninfo: %w", errno)
	}
	if v.BitsPerPixel != 32 {
		return nil, fmt.Errorf("fb: bpp %d, want 32", v.BitsPerPixel)
	}
	if v.Xres == 0 || v.Yres == 0 {
		return nil, fmt.Errorf("fb: empty geometry")
	}
	fix := make([]byte, fixInfoSize())
	_, _, errno = syscall.Syscall(syscall.SYS_IOCTL, f.Fd(), fbIOGetFScreenInfo, uintptr(unsafe.Pointer(&fix[0])))
	runtime.KeepAlive(fix)
	if errno != 0 {
		return nil, fmt.Errorf("fb: fix screeninfo: %w", errno)
	}
	smemOff, lineOff := fixFieldOffsets()
	smemLen := binary.LittleEndian.Uint32(fix[smemOff : smemOff+4])
	line := int(binary.LittleEndian.Uint32(fix[lineOff : lineOff+4]))
	minLine := int(v.Xres) * 4
	if line < minLine {
		line = minLine
	}
	length := int(smemLen)
	if length <= 0 {
		yv := int(v.YresVirtual)
		if yv < int(v.Yres) {
			yv = int(v.Yres)
		}
		length = yv * line
	}
	mem, err := syscall.Mmap(int(f.Fd()), 0, length, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_SHARED)
	if err != nil {
		return nil, fmt.Errorf("fb: mmap: %w", err)
	}
	return &FBSink{
		f:    f,
		mem:  mem,
		line: line,
		xres: int(v.Xres),
		yres: int(v.Yres),
		xoff: int(v.Xoffset),
		yoff: int(v.Yoffset),
		bpp:  4,
	}, nil
}

// Write converts img and copies each row at the framebuffer stride.
func (s *FBSink) Write(img *image.RGBA) error {
	if img == nil {
		return fmt.Errorf("fb: nil image")
	}
	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	if w > s.xres {
		w = s.xres
	}
	if h > s.yres {
		h = s.yres
	}
	stride := b.Dx() * 4
	n := stride * b.Dy()
	if cap(s.pack) < n {
		s.pack = make([]byte, n)
	}
	s.pack = s.pack[:n]
	ToBGRA32(s.pack, stride, img)
	blitBGRA(s.mem, s.line, s.xoff, s.yoff, s.bpp, s.pack, stride, w, h)
	return nil
}

// blitBGRA copies w*bpp bytes per row. Rows that do not fit are skipped.
func blitBGRA(mem []byte, lineLen, xoff, yoff, bpp int, src []byte, srcStride, w, h int) {
	if bpp <= 0 || lineLen <= 0 || w <= 0 || h <= 0 {
		return
	}
	row := w * bpp
	for y := 0; y < h; y++ {
		dstOff := (y+yoff)*lineLen + xoff*bpp
		srcOff := y * srcStride
		n := row
		if dstOff < 0 || srcOff < 0 || srcOff+n > len(src) || dstOff+n > len(mem) {
			continue
		}
		copy(mem[dstOff:dstOff+n], src[srcOff:srcOff+n])
	}
}

// Close unmaps and closes the device.
func (s *FBSink) Close() error {
	if s == nil {
		return nil
	}
	var err error
	if s.mem != nil {
		err = syscall.Munmap(s.mem)
		s.mem = nil
	}
	if s.f != nil {
		if cerr := s.f.Close(); err == nil {
			err = cerr
		}
		s.f = nil
	}
	return err
}

var (
	_ Sink = (*MemSink)(nil)
	_ Sink = (*PNGSink)(nil)
	_ Sink = (*FBSink)(nil)
)
