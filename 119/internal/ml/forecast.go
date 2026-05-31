package ml

import (
	"fmt"
	"math"
	"sort"
	"time"
)

type ProphetForecaster struct {
	verbose          bool
	config           ModelConfig
	seasonalityType  string
	changepointScale float64
	seasonalityScale float64
}

func NewProphetForecaster(verbose bool, config ModelConfig) *ProphetForecaster {
	return &ProphetForecaster{
		verbose:          verbose,
		config:           config,
		seasonalityType:  config.SeasonalityPeriod,
		changepointScale: 0.05,
		seasonalityScale: 10.0,
	}
}

type timeSeriesData struct {
	timestamps []time.Time
	values     []float64
}

func (f *ProphetForecaster) Forecast(
	entries []QueryLogEntry,
	tableName string,
	partitionName string,
) (*ForecastResult, error) {
	if f.verbose {
		fmt.Printf("[ml] forecasting for table=%s partition=%s\n", tableName, partitionName)
	}

	ts := f.buildHourlyTimeSeries(entries, tableName, partitionName)
	if len(ts.timestamps) < f.config.MinDataPoints {
		return nil, fmt.Errorf("insufficient data for forecasting: %d points < %d required",
			len(ts.timestamps), f.config.MinDataPoints)
	}

	trend := f.fitTrend(ts)
	seasonal := f.fitSeasonality(ts, trend)

	forecastStart := time.Now().Truncate(time.Hour)
	forecastEnd := forecastStart.Add(time.Duration(f.config.ForecastDays*24) * time.Hour)

	result := &ForecastResult{
		TableName:       tableName,
		PartitionName:   partitionName,
		ForecastHorizon: fmt.Sprintf("%dh", f.config.ForecastDays*24),
		Trend:           trend.slope,
		Seasonality:     seasonal.amplitude,
	}

	for i := range ts.timestamps {
		result.Historical = append(result.Historical, TimeSeriesPoint{
			Timestamp: ts.timestamps[i],
			Value:     ts.values[i],
		})
	}

	totalPredicted := 0.0
	peakPredicted := 0.0
	for t := forecastStart; t.Before(forecastEnd); t = t.Add(time.Hour) {
		predicted := f.predict(t, trend, seasonal, ts)
		confidence := 0.9

		residual := f.computeResidualStddev(ts, trend, seasonal)
		upperBound := predicted + 2.0*residual
		lowerBound := math.Max(0, predicted-2.0*residual)

		if upperBound < 0 {
			upperBound = predicted
		}

		result.Forecast = append(result.Forecast, ForecastPoint{
			Timestamp:      t,
			PredictedValue: predicted,
			UpperBound:     upperBound,
			LowerBound:     lowerBound,
			Confidence:     confidence,
		})

		totalPredicted += predicted
		if predicted > peakPredicted {
			peakPredicted = predicted
		}
	}

	result.PredictedLoad = peakPredicted

	loadThreshold := f.computeLoadThreshold(ts)
	result.ShouldOptimize = peakPredicted > loadThreshold*0.8 ||
		trend.slope > 0.1 ||
		(totalPredicted > loadThreshold*float64(f.config.ForecastDays*24)*0.5)

	if result.ShouldOptimize {
		result.OptimizeWindow = f.determineOptimizeWindow(ts)
	}

	return result, nil
}

type trendModel struct {
	slope     float64
	intercept float64
}

type seasonalModel struct {
	amplitude  float64
	phase      float64
	period     float64
}

func (f *ProphetForecaster) buildHourlyTimeSeries(
	entries []QueryLogEntry,
	tableName string,
	partitionName string,
) timeSeriesData {
	type hourBucket struct {
		t     time.Time
		count int64
	}

	bucketMap := map[int64]*hourBucket{}

	for _, e := range entries {
		if e.TableName != tableName {
			continue
		}
		if partitionName != "" && e.PartitionName != "" && e.PartitionName != partitionName {
			continue
		}

		hourKey := e.StartTime.Truncate(time.Hour).Unix()
		b, ok := bucketMap[hourKey]
		if !ok {
			b = &hourBucket{t: e.StartTime.Truncate(time.Hour)}
			bucketMap[hourKey] = b
		}
		b.count++
	}

	var buckets []*hourBucket
	for _, b := range bucketMap {
		buckets = append(buckets, b)
	}
	sort.Slice(buckets, func(i, j int) bool {
		return buckets[i].t.Before(buckets[j].t)
	})

	ts := timeSeriesData{}
	if len(buckets) == 0 {
		return ts
	}

	start := buckets[0].t
	end := buckets[len(buckets)-1].t

	bucketIndex := 0
	for t := start; !t.After(end); t = t.Add(time.Hour) {
		var count float64
		if bucketIndex < len(buckets) && buckets[bucketIndex].t.Equal(t) {
			count = float64(buckets[bucketIndex].count)
			bucketIndex++
		} else {
			count = 0
		}
		ts.timestamps = append(ts.timestamps, t)
		ts.values = append(ts.values, count)
	}

	return ts
}

func (f *ProphetForecaster) fitTrend(ts timeSeriesData) trendModel {
	n := len(ts.values)
	if n == 0 {
		return trendModel{}
	}

	var sumX, sumY, sumXY, sumXX float64
	for i, v := range ts.values {
		x := float64(i)
		sumX += x
		sumY += v
		sumXY += x * v
		sumXX += x * x
	}

	denominator := float64(n)*sumXX - sumX*sumX
	if math.Abs(denominator) < 1e-9 {
		return trendModel{slope: 0, intercept: sumY / float64(n)}
	}

	slope := (float64(n)*sumXY - sumX*sumY) / denominator
	intercept := (sumY - slope*sumX) / float64(n)

	return trendModel{
		slope:     slope,
		intercept: intercept,
	}
}

func (f *ProphetForecaster) fitSeasonality(ts timeSeriesData, trend trendModel) seasonalModel {
	n := len(ts.values)
	if n < 24 {
		return seasonalModel{amplitude: 0, phase: 0, period: 24}
	}

	residuals := make([]float64, n)
	for i, v := range ts.values {
		trendVal := trend.intercept + trend.slope*float64(i)
		residuals[i] = v - trendVal
	}

	period := 24.0
	switch f.seasonalityType {
	case "weekly":
		period = 168.0
	case "monthly":
		period = 720.0
	}

	if len(residuals) < int(period) {
		period = 24.0
	}

	var maxAbs float64
	hourSums := make(map[int]float64)
	hourCounts := make(map[int]int)

	for i, r := range residuals {
		hour := ts.timestamps[i].Hour()
		hourSums[hour] += r
		hourCounts[hour]++
		if math.Abs(r) > maxAbs {
			maxAbs = math.Abs(r)
		}
	}

	amplitude := 0.0
	if len(hourSums) > 0 {
		for hour := range hourSums {
			if hourCounts[hour] > 0 {
				avg := hourSums[hour] / float64(hourCounts[hour])
				if math.Abs(avg) > amplitude {
					amplitude = math.Abs(avg)
				}
			}
		}
	}

	return seasonalModel{
		amplitude: amplitude,
		phase:     0,
		period:    period,
	}
}

func (f *ProphetForecaster) predict(
	t time.Time,
	trend trendModel,
	seasonal seasonalModel,
	ts timeSeriesData,
) float64 {
	if len(ts.timestamps) == 0 {
		return 0
	}

	hoursSinceStart := t.Sub(ts.timestamps[0]).Hours()
	if hoursSinceStart < 0 {
		hoursSinceStart = 0
	}

	trendVal := trend.intercept + trend.slope*hoursSinceStart

	seasonalVal := 0.0
	if seasonal.period > 0 && seasonal.amplitude > 0 {
		hourOfDay := float64(t.Hour())
		dayOfWeek := float64(t.Weekday())
		seasonalVal = seasonal.amplitude *
			math.Sin(2*math.Pi*hourOfDay/24.0+seasonal.phase) *
			(0.7 + 0.3*math.Sin(2*math.Pi*dayOfWeek/7.0))
	}

	predicted := trendVal + seasonalVal
	return math.Max(0, predicted)
}

func (f *ProphetForecaster) computeResidualStddev(
	ts timeSeriesData,
	trend trendModel,
	seasonal seasonalModel,
) float64 {
	if len(ts.values) == 0 {
		return 0
	}

	var sumSq float64
	n := 0
	for i, v := range ts.values {
		predicted := f.predict(ts.timestamps[i], trend, seasonal, ts)
		residual := v - predicted
		sumSq += residual * residual
		n++
	}

	if n == 0 {
		return 0
	}

	return math.Sqrt(sumSq / float64(n))
}

func (f *ProphetForecaster) computeLoadThreshold(ts timeSeriesData) float64 {
	if len(ts.values) == 0 {
		return 0
	}

	var sorted []float64
	sorted = append(sorted, ts.values...)
	sort.Float64s(sorted)

	idx := int(float64(len(sorted)) * 0.8)
	if idx >= len(sorted) {
		idx = len(sorted) - 1
	}
	if idx < 0 {
		idx = 0
	}

	return sorted[idx]
}

func (f *ProphetForecaster) determineOptimizeWindow(ts timeSeriesData) string {
	if len(ts.values) == 0 {
		return "off-peak"
	}

	hourCounts := make(map[int]float64)
	hourMin := make(map[int]float64)
	hourMax := make(map[int]float64)

	for i, v := range ts.values {
		h := ts.timestamps[i].Hour()
		hourCounts[h] += v
		if _, ok := hourMin[h]; !ok || v < hourMin[h] {
			hourMin[h] = v
		}
		if v > hourMax[h] {
			hourMax[h] = v
		}
	}

	minHour := 0
	minCount := math.MaxFloat64
	for h, c := range hourCounts {
		if c < minCount {
			minCount = c
			minHour = h
		}
	}

	maxHour := 0
	maxCount := -1.0
	for h, c := range hourCounts {
		if c > maxCount {
			maxCount = c
			maxHour = h
		}
	}

	return fmt.Sprintf("low-load: %02d:00-%02d:00 (peak: %02d:00-%02d:00)",
		minHour, minHour+2, maxHour, maxHour+2)
}
