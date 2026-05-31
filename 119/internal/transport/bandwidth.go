package transport

import (
	"context"
	"fmt"
	"io"
	"sync/atomic"
	"time"
)

type BandwidthLimiter struct {
	rateLimitBytes int64
	bytesRead      int64
	bytesWritten   int64
	lastReset      time.Time
	ctx            context.Context
}

func NewBandwidthLimiter(rateMBPerSec int64) *BandwidthLimiter {
	return &BandwidthLimiter{
		rateLimitBytes: rateMBPerSec * 1024 * 1024,
		lastReset:      time.Now(),
	}
}

func (b *BandwidthLimiter) NewReader(r io.Reader) io.Reader {
	return &bandwidthLimitedReader{
		reader:  r,
		limiter: b,
	}
}

func (b *BandwidthLimiter) NewWriter(w io.Writer) io.Writer {
	return &bandwidthLimitedWriter{
		writer:  w,
		limiter: b,
	}
}

func (b *BandwidthLimiter) Allow(n int64) {
	for {
		elapsed := time.Since(b.lastReset).Seconds()
		if elapsed >= 1.0 {
			atomic.StoreInt64(&b.bytesRead, 0)
			atomic.StoreInt64(&b.bytesWritten, 0)
			b.lastReset = time.Now()
			elapsed = 0
		}

		currentRead := atomic.LoadInt64(&b.bytesRead)
		currentWrite := atomic.LoadInt64(&b.bytesWritten)
		total := currentRead + currentWrite

		if b.rateLimitBytes <= 0 || total+n <= b.rateLimitBytes {
			return
		}

		waitTime := time.Duration((float64(total+n-b.rateLimitBytes) / float64(b.rateLimitBytes)) * 1e9)
		if waitTime < 10*time.Millisecond {
			waitTime = 10 * time.Millisecond
		}
		time.Sleep(waitTime)
	}
}

func (b *BandwidthLimiter) CurrentRate() float64 {
	elapsed := time.Since(b.lastReset).Seconds()
	if elapsed <= 0 {
		return 0
	}
	total := atomic.LoadInt64(&b.bytesRead) + atomic.LoadInt64(&b.bytesWritten)
	return float64(total) / elapsed / (1024 * 1024)
}

type bandwidthLimitedReader struct {
	reader  io.Reader
	limiter *BandwidthLimiter
}

func (r *bandwidthLimitedReader) Read(p []byte) (int, error) {
	n, err := r.reader.Read(p)
	if n > 0 {
		r.limiter.Allow(int64(n))
		atomic.AddInt64(&r.limiter.bytesRead, int64(n))
	}
	return n, err
}

type bandwidthLimitedWriter struct {
	writer  io.Writer
	limiter *BandwidthLimiter
}

func (w *bandwidthLimitedWriter) Write(p []byte) (int, error) {
	w.limiter.Allow(int64(len(p)))
	n, err := w.writer.Write(p)
	if n > 0 {
		atomic.AddInt64(&w.limiter.bytesWritten, int64(n))
	}
	return n, err
}

type SpeedMonitor struct {
	startTime   time.Time
	totalBytes  int64
	lastBytes   int64
	lastTime    time.Time
	currentRate float64
}

func NewSpeedMonitor() *SpeedMonitor {
	now := time.Now()
	return &SpeedMonitor{
		startTime: now,
		lastTime:  now,
	}
}

func (m *SpeedMonitor) AddBytes(n int64) {
	m.totalBytes += n
}

func (m *SpeedMonitor) CurrentRateMBps() float64 {
	now := time.Now()
	elapsed := now.Sub(m.lastTime).Seconds()
	if elapsed < 1.0 {
		return m.currentRate
	}
	delta := m.totalBytes - m.lastBytes
	m.currentRate = float64(delta) / elapsed / (1024 * 1024)
	m.lastBytes = m.totalBytes
	m.lastTime = now
	return m.currentRate
}

func (m *SpeedMonitor) AverageRateMBps() float64 {
	elapsed := time.Since(m.startTime).Seconds()
	if elapsed <= 0 {
		return 0
	}
	return float64(m.totalBytes) / elapsed / (1024 * 1024)
}

func (m *SpeedMonitor) TotalMB() float64 {
	return float64(m.totalBytes) / (1024 * 1024)
}

func (m *SpeedMonitor) ETA(totalBytes int64) string {
	rate := m.CurrentRateMBps()
	if rate <= 0 {
		return "--:--"
	}
	remaining := float64(totalBytes-m.totalBytes) / (1024 * 1024)
	etaSec := remaining / rate
	if etaSec < 0 {
		etaSec = 0
	}
	hours := int(etaSec) / 3600
	minutes := (int(etaSec) % 3600) / 60
	seconds := int(etaSec) % 60
	if hours > 0 {
		return fmt.Sprintf("%02d:%02d:%02d", hours, minutes, seconds)
	}
	return fmt.Sprintf("%02d:%02d", minutes, seconds)
}
