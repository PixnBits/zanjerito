package soil

import (
	"context"
	"sync"
	"time"
)

// Fake is a Source whose days and error are set by tests.
type Fake struct {
	mu   sync.Mutex
	days []DayET
	err  error
}

// Set replaces the days returned by Fetch and the error. A non-nil err
// wins over days. Safe for concurrent use.
func (f *Fake) Set(days []DayET, err error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.days = append([]DayET(nil), days...)
	f.err = err
}

// Fetch returns the current days or error. A cancelled ctx wins.
func (f *Fake) Fetch(ctx context.Context, _ time.Time, _ int) ([]DayET, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.err != nil {
		return nil, f.err
	}
	out := make([]DayET, len(f.days))
	copy(out, f.days)
	return out, nil
}
