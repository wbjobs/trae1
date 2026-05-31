package main

import (
	"context"
	"fmt"
	"log"
	"strconv"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

type IndicatorQueryRequest struct {
	Code      string `form:"code" binding:"required"`
	Indicator string `form:"indicator" binding:"required"`
	StartTs   int64  `form:"start_ts"`
	EndTs     int64  `form:"end_ts"`
	Limit     int64  `form:"limit"`
}

type PeriodIndicatorQueryRequest struct {
	Code      string `form:"code" binding:"required"`
	Indicator string `form:"indicator" binding:"required"`
	Period    int64  `form:"period" binding:"required"`
	StartTs   int64  `form:"start_ts"`
	EndTs     int64  `form:"end_ts"`
	Limit     int64  `form:"limit"`
}

type ComparisonRequest struct {
	Code      string  `form:"code" binding:"required"`
	Indicator string  `form:"indicator" binding:"required"`
	Periods   string  `form:"periods"`
	StartTs   int64   `form:"start_ts"`
	EndTs     int64   `form:"end_ts"`
	Limit     int64   `form:"limit"`
}

type KLineQueryRequest struct {
	Code    string `form:"code" binding:"required"`
	Period  int64  `form:"period" binding:"required"`
	StartTs int64  `form:"start_ts"`
	EndTs   int64  `form:"end_ts"`
	Limit   int64  `form:"limit"`
}

type RecomputeRequest struct {
	Code    string `json:"code" binding:"required"`
	Period  int64  `json:"period" binding:"required"`
	StartTs int64  `json:"start_ts"`
	EndTs   int64  `json:"end_ts"`
}

type IndicatorPoint struct {
	Timestamp int64              `json:"timestamp"`
	Values    map[string]float64 `json:"values"`
}

type IndicatorResponse struct {
	Code      string            `json:"code"`
	Indicator string            `json:"indicator"`
	Period    string            `json:"period,omitempty"`
	Points    []IndicatorPoint  `json:"points"`
}

type ComparisonSeries struct {
	Period string           `json:"period"`
	Points []IndicatorPoint `json:"points"`
}

type ComparisonResponse struct {
	Code      string             `json:"code"`
	Indicator string             `json:"indicator"`
	Series    []ComparisonSeries `json:"series"`
}

type KLinePoint struct {
	Timestamp int64   `json:"timestamp"`
	Open      float64 `json:"open"`
	High      float64 `json:"high"`
	Low       float64 `json:"low"`
	Close     float64 `json:"close"`
	Volume    int64   `json:"volume"`
}

type KLineResponse struct {
	Code   string       `json:"code"`
	Period string       `json:"period"`
	Points []KLinePoint `json:"points"`
}

type StatsResponse struct {
	TicksProcessed int64 `json:"ticks_processed"`
	BatchCount     int64 `json:"batch_count"`
	LuaErrors      int64 `json:"lua_errors"`
	AvgLatencyUs   int64 `json:"avg_latency_us"`
}

type DataQualityResponse struct {
	Code             string  `json:"code"`
	CompletenessPct  float64 `json:"completeness_pct"`
	FillCount        int64   `json:"fill_count"`
	TotalReal        int64   `json:"total_real"`
	DataInsufficient bool    `json:"data_insufficient"`
	RecoveryCount    int64   `json:"recovery_count"`
	LastTickTs       int64   `json:"last_tick_ts"`
	TotalTicks       int64   `json:"total_ticks"`
	Status           string  `json:"status"`
}

type PeriodItem struct {
	Period int64  `json:"period"`
	Label  string `json:"label"`
}

type HTTPServer struct {
	redisCli        *RedisClient
	processor       *Processor
	klineAggregator *KLineAggregator
	periodMgr       *PeriodManager
	router          *gin.Engine
	addr            string
}

func NewHTTPServer(redisCli *RedisClient, processor *Processor,
	klineAggregator *KLineAggregator, periodMgr *PeriodManager, addr string) *HTTPServer {
	gin.SetMode(gin.ReleaseMode)
	r := gin.New()
	r.Use(gin.Recovery())

	srv := &HTTPServer{
		redisCli:        redisCli,
		processor:       processor,
		klineAggregator: klineAggregator,
		periodMgr:       periodMgr,
		router:          r,
		addr:            addr,
	}

	r.GET("/api/indicator", srv.QueryIndicator)
	r.GET("/api/period_indicator", srv.QueryPeriodIndicator)
	r.GET("/api/comparison", srv.QueryComparison)
	r.GET("/api/kline", srv.QueryKLine)
	r.GET("/api/stats", srv.GetStats)
	r.GET("/api/data_quality", srv.GetDataQuality)
	r.GET("/api/periods", srv.ListPeriods)
	r.POST("/api/periods", srv.AddPeriod)
	r.DELETE("/api/periods/:period", srv.RemovePeriod)
	r.POST("/api/recompute", srv.Recompute)
	r.GET("/health", srv.HealthCheck)

	return srv
}

func (s *HTTPServer) QueryIndicator(c *gin.Context) {
	var req IndicatorQueryRequest
	if err := c.ShouldBindQuery(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	validIndicators := map[string]bool{
		"MA5": true, "MA10": true, "MACD": true, "KDJ": true, "RSI": true,
	}
	if !validIndicators[req.Indicator] {
		c.JSON(400, gin.H{"error": "invalid indicator, must be one of: MA5, MA10, MACD, KDJ, RSI"})
		return
	}

	key := fmt.Sprintf("stock:%s:%s", req.Code, req.Indicator)
	points := s.queryIndicatorFromRedis(c, key, req.Indicator, req.StartTs, req.EndTs, req.Limit)

	c.JSON(200, IndicatorResponse{
		Code:      req.Code,
		Indicator: req.Indicator,
		Points:    points,
	})
}

func (s *HTTPServer) QueryPeriodIndicator(c *gin.Context) {
	var req PeriodIndicatorQueryRequest
	if err := c.ShouldBindQuery(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	validIndicators := map[string]bool{
		"MA5": true, "MA10": true, "MACD": true, "KDJ": true, "RSI": true,
	}
	if !validIndicators[req.Indicator] {
		c.JSON(400, gin.H{"error": "invalid indicator"})
		return
	}

	if !s.periodMgr.HasPeriod(req.Period) {
		c.JSON(400, gin.H{"error": "period not active"})
		return
	}

	key := fmt.Sprintf("stock:%s:%d:%s", req.Code, req.Period, req.Indicator)
	points := s.queryIndicatorFromRedis(c, key, req.Indicator, req.StartTs, req.EndTs, req.Limit)

	c.JSON(200, IndicatorResponse{
		Code:      req.Code,
		Indicator: req.Indicator,
		Period:    PeriodLabel(req.Period),
		Points:    points,
	})
}

func (s *HTTPServer) QueryComparison(c *gin.Context) {
	var req ComparisonRequest
	if err := c.ShouldBindQuery(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	validIndicators := map[string]bool{
		"MA5": true, "MA10": true, "MACD": true, "KDJ": true, "RSI": true,
	}
	if !validIndicators[req.Indicator] {
		c.JSON(400, gin.H{"error": "invalid indicator"})
		return
	}

	var periods []int64
	if req.Periods != "" {
		for _, p := range strings.Split(req.Periods, ",") {
			pv, err := strconv.ParseInt(strings.TrimSpace(p), 10, 64)
			if err == nil && s.periodMgr.HasPeriod(pv) {
				periods = append(periods, pv)
			}
		}
	}
	if len(periods) == 0 {
		periods = s.periodMgr.GetPeriods()
	}

	series := make([]ComparisonSeries, 0, len(periods))
	for _, period := range periods {
		key := fmt.Sprintf("stock:%s:%d:%s", req.Code, period, req.Indicator)
		points := s.queryIndicatorFromRedis(c, key, req.Indicator, req.StartTs, req.EndTs, req.Limit)
		series = append(series, ComparisonSeries{
			Period: PeriodLabel(period),
			Points: points,
		})
	}

	c.JSON(200, ComparisonResponse{
		Code:      req.Code,
		Indicator: req.Indicator,
		Series:    series,
	})
}

func (s *HTTPServer) QueryKLine(c *gin.Context) {
	var req KLineQueryRequest
	if err := c.ShouldBindQuery(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	if !s.periodMgr.HasPeriod(req.Period) {
		c.JSON(400, gin.H{"error": "period not active"})
		return
	}

	key := fmt.Sprintf("kline:%s:%d", req.Code, req.Period)

	ctx := context.Background()
	var minScore, maxScore string
	if req.StartTs > 0 {
		minScore = strconv.FormatInt(req.StartTs/1000, 10)
	} else {
		minScore = "-inf"
	}
	if req.EndTs > 0 {
		maxScore = strconv.FormatInt(req.EndTs/1000, 10)
	} else {
		maxScore = "+inf"
	}

	limit := req.Limit
	if limit <= 0 || limit > 500 {
		limit = 100
	}

	members, err := s.redisCli.ZRangeByScore(ctx, key, minScore, maxScore, 0, limit)
	if err != nil {
		c.JSON(500, gin.H{"error": "query failed: " + err.Error()})
		return
	}

	points := make([]KLinePoint, 0, len(members))
	for _, member := range members {
		parts := splitMember(member)
		if len(parts) < 6 {
			continue
		}
		ts, _ := strconv.ParseInt(parts[0], 10, 64)
		o, _ := strconv.ParseFloat(parts[1], 64)
		h, _ := strconv.ParseFloat(parts[2], 64)
		l, _ := strconv.ParseFloat(parts[3], 64)
		cl, _ := strconv.ParseFloat(parts[4], 64)
		v, _ := strconv.ParseInt(parts[5], 10, 64)

		points = append(points, KLinePoint{
			Timestamp: ts * 1000,
			Open:      o,
			High:      h,
			Low:       l,
			Close:     cl,
			Volume:    v,
		})
	}

	c.JSON(200, KLineResponse{
		Code:   req.Code,
		Period: PeriodLabel(req.Period),
		Points: points,
	})
}

func (s *HTTPServer) queryIndicatorFromRedis(c *gin.Context, key, indicator string,
	startTs, endTs, limit int64) []IndicatorPoint {

	ctx := context.Background()

	var minScore, maxScore string
	if startTs > 0 {
		minScore = strconv.FormatInt(startTs, 10)
	} else {
		minScore = "-inf"
	}
	if endTs > 0 {
		maxScore = strconv.FormatInt(endTs, 10)
	} else {
		maxScore = "+inf"
	}

	if limit <= 0 || limit > 100 {
		limit = 100
	}

	members, err := s.redisCli.ZRangeByScore(ctx, key, minScore, maxScore, 0, limit)
	if err != nil {
		return nil
	}

	return parseIndicatorMembers(indicator, members)
}

func (s *HTTPServer) ListPeriods(c *gin.Context) {
	periods := s.periodMgr.GetPeriods()
	items := make([]PeriodItem, 0, len(periods))
	for _, p := range periods {
		items = append(items, PeriodItem{
			Period: p,
			Label:  PeriodLabel(p),
		})
	}
	c.JSON(200, items)
}

func (s *HTTPServer) AddPeriod(c *gin.Context) {
	var body struct {
		Period int64 `json:"period" binding:"required"`
	}
	if err := c.ShouldBindJSON(&body); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	if body.Period < 10 || body.Period > 86400 {
		c.JSON(400, gin.H{"error": "period must be between 10 and 86400 seconds"})
		return
	}

	ctx := context.Background()
	if err := s.periodMgr.AddPeriod(ctx, body.Period); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Period %d (%s) added dynamically", body.Period, PeriodLabel(body.Period))
	c.JSON(200, gin.H{"status": "ok", "period": body.Period, "label": PeriodLabel(body.Period)})
}

func (s *HTTPServer) RemovePeriod(c *gin.Context) {
	periodStr := c.Param("period")
	period, err := strconv.ParseInt(periodStr, 10, 64)
	if err != nil {
		c.JSON(400, gin.H{"error": "invalid period"})
		return
	}

	ctx := context.Background()
	if err := s.periodMgr.RemovePeriod(ctx, period); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Period %d (%s) removed dynamically", period, PeriodLabel(period))
	c.JSON(200, gin.H{"status": "ok"})
}

func (s *HTTPServer) Recompute(c *gin.Context) {
	var req RecomputeRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(400, gin.H{"error": err.Error()})
		return
	}

	if !s.periodMgr.HasPeriod(req.Period) {
		c.JSON(400, gin.H{"error": "period not active"})
		return
	}

	ctx := context.Background()
	if err := s.klineAggregator.RecomputePeriod(ctx, req.Code, req.Period, req.StartTs, req.EndTs); err != nil {
		c.JSON(500, gin.H{"error": "recompute failed: " + err.Error()})
		return
	}

	c.JSON(200, gin.H{"status": "ok", "message": "recompute completed"})
}

func (s *HTTPServer) GetStats(c *gin.Context) {
	stats := s.processor.GetStats()
	klineStats := s.klineAggregator.GetStats()
	c.JSON(200, gin.H{
		"tick_indicator": StatsResponse{
			TicksProcessed: stats.TicksProcessed.Load(),
			BatchCount:     stats.BatchCount.Load(),
			LuaErrors:      stats.LuaErrors.Load(),
			AvgLatencyUs:   stats.AvgLatencyUs.Load(),
		},
		"kline_period": gin.H{
			"ticks_processed":  klineStats.TicksProcessed.Load(),
			"klines_created":   klineStats.KLinesCreated.Load(),
			"klines_updated":   klineStats.KLinesUpdated.Load(),
			"indicators_calc":  klineStats.IndicatorsCalc.Load(),
			"lua_errors":       klineStats.LuaErrors.Load(),
		},
	})
}

func (s *HTTPServer) GetDataQuality(c *gin.Context) {
	code := c.Query("code")
	if code == "" {
		c.JSON(400, gin.H{"error": "code is required"})
		return
	}

	ctx := context.Background()
	stateKey := fmt.Sprintf("stock:%s:state", code)

	state, err := s.redisCli.HGetAll(ctx, stateKey)
	if err != nil {
		c.JSON(500, gin.H{"error": "query failed: " + err.Error()})
		return
	}

	if len(state) == 0 {
		c.JSON(200, DataQualityResponse{
			Code:            code,
			CompletenessPct: 0,
			Status:          "no_data",
		})
		return
	}

	toInt64 := func(s string) int64 {
		v, _ := strconv.ParseInt(s, 10, 64)
		return v
	}

	totalReal := toInt64(state["total_real"])
	fillCount := toInt64(state["gap_fill_count"])
	dataInsufficient := toInt64(state["data_insufficient"])
	recoveryCount := toInt64(state["recovery_count"])
	lastTickTs := toInt64(state["last_tick_ts"])
	totalTicks := toInt64(state["count"])

	var completenessPct float64
	totalAll := totalReal + fillCount
	if totalAll > 0 {
		completenessPct = float64(totalReal) / float64(totalAll) * 100.0
	}

	status := "normal"
	if dataInsufficient == 1 {
		status = "data_insufficient"
	} else if fillCount > 0 {
		status = "partial_fill"
	}

	c.JSON(200, DataQualityResponse{
		Code:             code,
		CompletenessPct:  completenessPct,
		FillCount:        fillCount,
		TotalReal:        totalReal,
		DataInsufficient: dataInsufficient == 1,
		RecoveryCount:    recoveryCount,
		LastTickTs:       lastTickTs,
		TotalTicks:       totalTicks,
		Status:           status,
	})
}

func (s *HTTPServer) HealthCheck(c *gin.Context) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	if err := s.redisCli.Ping(ctx); err != nil {
		c.JSON(503, gin.H{"status": "unhealthy", "redis": "disconnected"})
		return
	}
	c.JSON(200, gin.H{"status": "healthy", "redis": "connected"})
}

func (s *HTTPServer) Start() error {
	return s.router.Run(s.addr)
}

func parseIndicatorMembers(indicator string, members []string) []IndicatorPoint {
	points := make([]IndicatorPoint, 0, len(members))

	for _, member := range members {
		parts := strings.SplitN(member, ":", 2)
		if len(parts) < 2 {
			continue
		}

		ts, err := strconv.ParseInt(parts[0], 10, 64)
		if err != nil {
			continue
		}

		values := make(map[string]float64)

		switch indicator {
		case "MA5", "MA10", "RSI":
			val, err := strconv.ParseFloat(parts[1], 64)
			if err != nil {
				continue
			}
			values[indicator] = val

		case "MACD":
			valParts := strings.Split(parts[1], ":")
			if len(valParts) >= 3 {
				values["DIF"], _ = strconv.ParseFloat(valParts[0], 64)
				values["DEA"], _ = strconv.ParseFloat(valParts[1], 64)
				values["MACD"], _ = strconv.ParseFloat(valParts[2], 64)
			}

		case "KDJ":
			valParts := strings.Split(parts[1], ":")
			if len(valParts) >= 3 {
				values["K"], _ = strconv.ParseFloat(valParts[0], 64)
				values["D"], _ = strconv.ParseFloat(valParts[1], 64)
				values["J"], _ = strconv.ParseFloat(valParts[2], 64)
			}
		}

		points = append(points, IndicatorPoint{
			Timestamp: ts,
			Values:    values,
		})
	}

	return points
}
