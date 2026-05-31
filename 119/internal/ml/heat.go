package ml

import (
	"fmt"
	"math"
	"time"
)

type HeatDetector struct {
	verbose    bool
	thresholds HeatThresholds
}

func NewHeatDetector(verbose bool, thresholds HeatThresholds) *HeatDetector {
	return &HeatDetector{
		verbose:    verbose,
		thresholds: thresholds,
	}
}

func (d *HeatDetector) SetThresholds(t HeatThresholds) {
	d.thresholds = t
}

func (d *HeatDetector) GetThresholds() HeatThresholds {
	return d.thresholds
}

func (d *HeatDetector) ComputeHeatScore(stats *PartitionQueryStats, lookbackDays int) float64 {
	if stats == nil {
		return 0
	}

	now := time.Now()
	cutoff := now.AddDate(0, 0, -lookbackDays)

	if stats.LastQueryTime.Before(cutoff) {
		return 0
	}

	recencyScore := 0.0
	if !stats.LastQueryTime.IsZero() {
		daysSinceLastQuery := now.Sub(stats.LastQueryTime).Hours() / 24
		recencyScore = math.Exp(-daysSinceLastQuery / 7.0)
	}

	frequencyScore := 0.0
	if d.thresholds.HotQueryCount > 0 {
		frequencyScore = math.Min(1.0, float64(stats.QueryCount)/float64(d.thresholds.HotQueryCount))
	}

	byteScore := 0.0
	if d.thresholds.HotBytesThreshold > 0 {
		byteScore = math.Min(1.0, float64(stats.BytesScanned)/float64(d.thresholds.HotBytesThreshold))
	}

	durationScore := 0.0
	if stats.QueryCount > 0 && stats.TotalDurationMs > 0 {
		avgMs := float64(stats.TotalDurationMs) / float64(stats.QueryCount)
		durationScore = math.Min(1.0, avgMs/1000.0)
	}

	weighted := 0.35*recencyScore + 0.30*frequencyScore + 0.20*byteScore + 0.15*durationScore

	return math.Min(1.0, math.Max(0.0, weighted))
}

func (d *HeatDetector) Classify(stats *PartitionQueryStats) string {
	stats.HeatScore = d.ComputeHeatScore(stats, d.thresholds.LookbackDays)

	if stats.HeatScore >= d.thresholds.HotHeatScore {
		stats.IsHot = true
		stats.IsWarm = false
		stats.IsCold = false
		return "hot"
	} else if stats.HeatScore <= d.thresholds.ColdHeatScore &&
		stats.QueryCount <= d.thresholds.ColdQueryCount &&
		stats.BytesScanned <= d.thresholds.ColdBytesThreshold {
		stats.IsHot = false
		stats.IsWarm = false
		stats.IsCold = true
		return "cold"
	} else {
		stats.IsHot = false
		stats.IsWarm = true
		stats.IsCold = false
		return "warm"
	}
}

func (d *HeatDetector) ClassifyTable(t *TableQueryStats) {
	t.HotPartitionCount = 0
	t.ColdPartitionCount = 0
	t.WarmPartitionCount = 0

	for _, p := range t.Partitions {
		class := d.Classify(p)
		switch class {
		case "hot":
			t.HotPartitionCount++
		case "cold":
			t.ColdPartitionCount++
		case "warm":
			t.WarmPartitionCount++
		}
	}
}

func (d *HeatDetector) GetHotPartitions(t *TableQueryStats) []string {
	var hot []string
	for key, p := range t.Partitions {
		if p.IsHot {
			hot = append(hot, key)
		}
	}
	return hot
}

func (d *HeatDetector) GetColdPartitions(t *TableQueryStats) []string {
	var cold []string
	for key, p := range t.Partitions {
		if p.IsCold {
			cold = append(cold, key)
		}
	}
	return cold
}

func (d *HeatDetector) PrintSummary(t *TableQueryStats) {
	fmt.Println("=== Partition Heat Analysis ===")
	fmt.Printf("Table: %s\n", t.TableName)
	fmt.Printf("Total Queries: %d\n", t.TotalQueries)
	fmt.Printf("Total Bytes Scanned: %.2f GB\n", float64(t.TotalBytes)/(1024*1024*1024))
	fmt.Printf("Hot Partitions: %d\n", t.HotPartitionCount)
	fmt.Printf("Warm Partitions: %d\n", t.WarmPartitionCount)
	fmt.Printf("Cold Partitions: %d\n", t.ColdPartitionCount)
	fmt.Println()

	fmt.Println("--- Hot Partitions ---")
	hotCount := 0
	for key, p := range t.Partitions {
		if p.IsHot {
			hotCount++
			fmt.Printf("  [%d] %s\n", hotCount, key)
			fmt.Printf("    Queries: %d, Bytes: %.2f GB, HeatScore: %.4f\n",
				p.QueryCount,
				float64(p.BytesScanned)/(1024*1024*1024),
				p.HeatScore)
			if hotCount >= 10 {
				remaining := t.HotPartitionCount - hotCount
				if remaining > 0 {
					fmt.Printf("  ... and %d more hot partitions\n", remaining)
				}
				break
			}
		}
	}
}
