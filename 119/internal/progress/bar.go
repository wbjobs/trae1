package progress

import (
	"fmt"
	"os"
	"sync"
	"time"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

type Bar struct {
	state       types.ProgressState
	description string
	width       int
	mu          sync.Mutex
	startTime   time.Time
}

func NewBar(description string, totalBytes int64) *Bar {
	return &Bar{
		state: types.ProgressState{
			Phase:      description,
			TotalBytes: totalBytes,
			StartTime:  time.Now(),
		},
		description: description,
		width:       40,
		startTime:   time.Now(),
	}
}

func (b *Bar) Update(transferred int64) {
	b.mu.Lock()
	defer b.mu.Unlock()

	b.state.Transferred = transferred
	if b.state.TotalBytes > 0 {
		b.state.Percent = float64(transferred) / float64(b.state.TotalBytes) * 100
	}
	elapsed := time.Since(b.startTime).Seconds()
	if elapsed > 0 {
		b.state.Speed = float64(transferred) / elapsed / (1024 * 1024)
	}
}

func (b *Bar) SetPhase(phase string) {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.state.Phase = phase
	b.state.Transferred = 0
	b.state.Percent = 0
	b.state.Speed = 0
	b.startTime = time.Now()
}

func (b *Bar) Render() string {
	b.mu.Lock()
	defer b.mu.Unlock()

	percent := b.state.Percent
	if percent > 100 {
		percent = 100
	}

	filled := int(float64(b.width) * percent / 100)
	bar := ""
	for i := 0; i < b.width; i++ {
		if i < filled {
			bar += "█"
		} else {
			bar += "░"
		}
	}

	var etaStr string
	if b.state.Speed > 0 && b.state.TotalBytes > 0 {
		remaining := float64(b.state.TotalBytes-b.state.Transferred) / (1024 * 1024)
		etaSec := remaining / b.state.Speed
		if etaSec < 0 {
			etaSec = 0
		}
		hours := int(etaSec) / 3600
		minutes := (int(etaSec) % 3600) / 60
		seconds := int(etaSec) % 60
		if hours > 0 {
			etaStr = fmt.Sprintf("%02d:%02d:%02d", hours, minutes, seconds)
		} else {
			etaStr = fmt.Sprintf("%02d:%02d", minutes, seconds)
		}
	} else {
		etaStr = "--:--"
	}

	transferredMB := float64(b.state.Transferred) / (1024 * 1024)
	totalMB := float64(b.state.TotalBytes) / (1024 * 1024)

	return fmt.Sprintf("\r%s [%s] %.1f%% (%.1f/%.1f MB) %.2f MB/s ETA: %s",
		b.state.Phase, bar, percent, transferredMB, totalMB, b.state.Speed, etaStr)
}

func (b *Bar) Finish() {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.state.Percent = 100
	b.state.Transferred = b.state.TotalBytes
}

func (b *Bar) GetState() *types.ProgressState {
	b.mu.Lock()
	defer b.mu.Unlock()
	state := b.state
	return &state
}

type MultiPhaseTracker struct {
	phases []string
	current int
	bar    *Bar
}

func NewMultiPhaseTracker(phases []string) *MultiPhaseTracker {
	return &MultiPhaseTracker{
		phases: phases,
		bar:    NewBar(phases[0], 0),
	}
}

func (t *MultiPhaseTracker) NextPhase(totalBytes int64) {
	t.current++
	if t.current < len(t.phases) {
		t.bar.SetPhase(t.phases[t.current])
	}
	t.bar = NewBar(t.phases[t.current], totalBytes)
}

func (t *MultiPhaseTracker) Update(transferred int64) {
	t.bar.Update(transferred)
}

func (t *MultiPhaseTracker) Render() string {
	return t.bar.Render()
}

type MigrationLogger struct {
	startTime time.Time
	verbose   bool
}

func NewLogger(verbose bool) *MigrationLogger {
	return &MigrationLogger{
		startTime: time.Now(),
		verbose:   verbose,
	}
}

func (l *MigrationLogger) Log(msg string) {
	elapsed := time.Since(l.startTime)
	fmt.Printf("[%s] %s\n", formatDuration(elapsed), msg)
}

func (l *MigrationLogger) Logf(format string, args ...interface{}) {
	elapsed := time.Since(l.startTime)
	fmt.Printf("[%s] %s\n", formatDuration(elapsed), fmt.Sprintf(format, args...))
}

func (l *MigrationLogger) Verbose(msg string) {
	if l.verbose {
		l.Log(msg)
	}
}

func (l *MigrationLogger) Verbosef(format string, args ...interface{}) {
	if l.verbose {
		l.Logf(format, args...)
	}
}

func (l *MigrationLogger) Error(msg string) {
	elapsed := time.Since(l.startTime)
	fmt.Fprintf(os.Stderr, "[%s] ERROR: %s\n", formatDuration(elapsed), msg)
}

func (l *MigrationLogger) Errorf(format string, args ...interface{}) {
	elapsed := time.Since(l.startTime)
	fmt.Fprintf(os.Stderr, "[%s] ERROR: %s\n", formatDuration(elapsed), fmt.Sprintf(format, args...))
}

func formatDuration(d time.Duration) string {
	hours := int(d.Hours())
	minutes := int(d.Minutes()) % 60
	seconds := d.Seconds() - float64(int(d.Hours())*3600+int(d.Minutes())%60*60)
	if hours > 0 {
		return fmt.Sprintf("%02d:%02d:%05.2f", hours, minutes, seconds)
	}
	return fmt.Sprintf("%02d:%05.2f", minutes, seconds)
}
