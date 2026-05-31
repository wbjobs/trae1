package progress

import (
	"fmt"
	"sync"
	"time"
)

type ProgressBar struct {
	description string
	startTime   time.Time
	total       int64
	current     int64
	mu          sync.Mutex
	done        bool
}

func NewProgressBar(description string) *ProgressBar {
	return &ProgressBar{
		description: description,
		startTime:   time.Now(),
	}
}

func (pb *ProgressBar) Start() {
	pb.startTime = time.Now()
	pb.current = 0
	pb.done = false
	fmt.Printf("\n%s: [", pb.description)
}

func (pb *ProgressBar) Update(transferred, total int64, speed float64) {
	pb.mu.Lock()
	defer pb.mu.Unlock()

	if pb.done {
		return
	}

	pb.current = transferred
	pb.total = total

	if total == 0 {
		return
	}

	percent := float64(transferred) / float64(total) * 100
	barWidth := 40
	filled := int(percent / 100 * float64(barWidth))

	bar := ""
	for i := 0; i < barWidth; i++ {
		if i < filled {
			bar += "█"
		} else {
			bar += "░"
		}
	}

	elapsed := time.Since(pb.startTime).Seconds()
	eta := ""
	if speed > 0 && transferred < total {
		remaining := float64(total-transferred) / speed
		eta = fmt.Sprintf(" ETA: %s", formatDuration(remaining))
	}

	fmt.Printf("\r%s: [%s] %.1f%% %s/s %s",
		pb.description,
		bar,
		percent,
		FormatBytes(int64(speed)),
		eta,
	)
}

func (pb *ProgressBar) Finish() {
	pb.mu.Lock()
	defer pb.mu.Unlock()

	if pb.done {
		return
	}
	pb.done = true

	elapsed := time.Since(pb.startTime).Seconds()
	fmt.Printf("\r%s: [████████████████████████████████████████] 100%% %s (%s)\n",
		pb.description,
		FormatBytes(pb.total),
		formatDuration(elapsed),
	)
}

func FormatBytes(bytes int64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := int64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

func formatDuration(seconds float64) string {
	if seconds < 60 {
		return fmt.Sprintf("%.1fs", seconds)
	}
	minutes := int(seconds) / 60
	secs := int(seconds) % 60
	return fmt.Sprintf("%dm%02ds", minutes, secs)
}
