package hamqtt

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"time"
)

const maxBody = 1 << 20 // 1 MiB

// NewHTTPFetcher GETs url. Non-2xx, a body larger than 1 MiB, or a transport
// error is returned as an error. timeout should be about the poll interval.
func NewHTTPFetcher(url string, timeout time.Duration) func(context.Context) ([]byte, error) {
	c := &http.Client{Timeout: timeout}
	return func(ctx context.Context) ([]byte, error) {
		req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
		if err != nil {
			return nil, err
		}
		req.Header.Set("Accept", "application/json")
		resp, err := c.Do(req)
		if err != nil {
			return nil, err
		}
		defer resp.Body.Close()
		if resp.StatusCode < 200 || resp.StatusCode > 299 {
			_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 4<<10))
			return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
		}
		data, err := io.ReadAll(io.LimitReader(resp.Body, maxBody+1))
		if err != nil {
			return nil, err
		}
		if len(data) > maxBody {
			return nil, fmt.Errorf("body exceeds %d bytes", maxBody)
		}
		return data, nil
	}
}
