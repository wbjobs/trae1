package main

import (
	"context"
	"fmt"
	"strconv"
	"sync"
)

const (
	PeriodsConfigKey = "config:periods"
)

type PeriodManager struct {
	redisCli *RedisClient
	periods  map[int64]bool
	mu       sync.RWMutex
}

func NewPeriodManager(redisCli *RedisClient) *PeriodManager {
	pm := &PeriodManager{
		redisCli: redisCli,
		periods:  make(map[int64]bool),
	}
	return pm
}

func (pm *PeriodManager) Init(ctx context.Context, defaults []int64) error {
	existing, err := pm.redisCli.HGetAll(ctx, PeriodsConfigKey)
	if err != nil {
		return err
	}

	if len(existing) == 0 {
		for _, p := range defaults {
			pm.periods[p] = true
			pm.redisCli.GetClient().HSet(ctx, PeriodsConfigKey, strconv.FormatInt(p, 10), "1")
		}
	} else {
		for k, v := range existing {
			if v == "1" {
				p, err := strconv.ParseInt(k, 10, 64)
				if err == nil {
					pm.periods[p] = true
				}
			}
		}
		if len(pm.periods) == 0 {
			for _, p := range defaults {
				pm.periods[p] = true
				pm.redisCli.GetClient().HSet(ctx, PeriodsConfigKey, strconv.FormatInt(p, 10), "1")
			}
		}
	}
	return nil
}

func (pm *PeriodManager) AddPeriod(ctx context.Context, periodSec int64) error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	if pm.periods[periodSec] {
		return fmt.Errorf("period %d already exists", periodSec)
	}

	pm.periods[periodSec] = true
	return pm.redisCli.GetClient().HSet(ctx, PeriodsConfigKey, strconv.FormatInt(periodSec, 10), "1").Err()
}

func (pm *PeriodManager) RemovePeriod(ctx context.Context, periodSec int64) error {
	pm.mu.Lock()
	defer pm.mu.Unlock()

	if !pm.periods[periodSec] {
		return fmt.Errorf("period %d not found", periodSec)
	}

	delete(pm.periods, periodSec)
	return pm.redisCli.GetClient().HDel(ctx, PeriodsConfigKey, strconv.FormatInt(periodSec, 10)).Err()
}

func (pm *PeriodManager) GetPeriods() []int64 {
	pm.mu.RLock()
	defer pm.mu.RUnlock()

	result := make([]int64, 0, len(pm.periods))
	for p := range pm.periods {
		result = append(result, p)
	}
	return result
}

func (pm *PeriodManager) HasPeriod(periodSec int64) bool {
	pm.mu.RLock()
	defer pm.mu.RUnlock()
	return pm.periods[periodSec]
}

func PeriodLabel(periodSec int64) string {
	switch periodSec {
	case 60:
		return "1min"
	case 300:
		return "5min"
	case 900:
		return "15min"
	case 3600:
		return "60min"
	default:
		return fmt.Sprintf("%dsec", periodSec)
	}
}
