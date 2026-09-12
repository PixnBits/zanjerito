package gpio

import "fmt"

// NewGpiocdev will wrap github.com/warthog618/go-gpiocdev with inactive-on-release.
// Real line request lands in a follow-up PR once pin-map loading exists; this stub
// keeps the driver name reserved and fails closed.
func NewGpiocdev() (Driver, error) {
	return nil, fmt.Errorf("gpiocdev driver not wired yet; use -driver=fake or -driver=lockout")
}
