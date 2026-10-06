package rain

import (
	"context"
	"time"
)

// Sample is one incremental rainfall report. Inches is the tip (or 0 for a
// heartbeat), not the gauge's cumulative total. A negative tip is kept.
type Sample struct {
	Time   time.Time
	Inches float64
}

// Source fetches incremental rainfall. Fetch must honor ctx. Implementations
// used by the poller are called from one goroutine at a time; Fake is safe
// for concurrent Set and Fetch.
type Source interface {
	Fetch(ctx context.Context) ([]Sample, error)
}
