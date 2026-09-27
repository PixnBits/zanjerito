package rain

import (
	"context"
	"sync"
)

// Fake is a Source whose samples and error are set by tests.
type Fake struct {
	mu      sync.Mutex
	samples []Sample
	err     error
}

// Set replaces the samples returned by Fetch and the error. A non-nil err
// wins over samples. Safe for concurrent use.
func (f *Fake) Set(samples []Sample, err error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.samples = append([]Sample(nil), samples...)
	f.err = err
}

// Fetch returns the current samples or error. A cancelled ctx wins.
func (f *Fake) Fetch(ctx context.Context) ([]Sample, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.err != nil {
		return nil, f.err
	}
	out := make([]Sample, len(f.samples))
	copy(out, f.samples)
	return out, nil
}
