package api

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"bastion/internal/models"
	"bastion/internal/terminal"
)

func (s *Server) handleSessionReplayURL(w http.ResponseWriter, r *http.Request, session *models.Session) {
	if session.Status != models.SessionStatusCompleted {
		writeError(w, http.StatusBadRequest, "session is still running or not completed")
		return
	}

	if session.ObjectKey == "" {
		writeError(w, http.StatusBadRequest, "recording not uploaded yet")
		return
	}

	ctx := r.Context()
	presignedURL, err := s.minio.GetPresignedURL(ctx, session.ObjectKey, s.cfg.API.SignURLExpire)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "generate url failed: "+err.Error())
		return
	}

	writeSuccess(w, map[string]interface{}{
		"url":        presignedURL,
		"expires_in": int(s.cfg.API.SignURLExpire.Seconds()),
		"object_key": session.ObjectKey,
	})
}

func (s *Server) handleSessionStream(w http.ResponseWriter, r *http.Request, session *models.Session) {
	if session.Status != models.SessionStatusRunning {
		writeError(w, http.StatusBadRequest, "session is not currently running")
		return
	}

	flusher, ok := w.(http.Flusher)
	if !ok {
		writeError(w, http.StatusInternalServerError, "streaming not supported")
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	notify := r.Context().Done()

	ch, done := session.LiveStream.Subscribe()
	defer session.LiveStream.Unsubscribe(ch)

	fmt.Fprintf(w, "event: connected\ndata: %s\n\n", session.ID)
	flusher.Flush()

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-notify:
			return
		case <-done:
			fmt.Fprintf(w, "event: session_ended\ndata: session %s has ended\n\n", session.ID)
			flusher.Flush()
			return
		case data, ok := <-ch:
			if !ok {
				return
			}
			encoded := jsonEscape(string(data))
			fmt.Fprintf(w, "event: output\ndata: %s\n\n", encoded)
			flusher.Flush()
		case <-ticker.C:
			fmt.Fprintf(w, "event: ping\ndata: keepalive\n\n")
			flusher.Flush()
		}
	}
}

func (s *Server) handleSessionStreamRendered(w http.ResponseWriter, r *http.Request, session *models.Session) {
	if session.Status != models.SessionStatusRunning {
		writeError(w, http.StatusBadRequest, "session is not currently running")
		return
	}

	flusher, ok := w.(http.Flusher)
	if !ok {
		writeError(w, http.StatusInternalServerError, "streaming not supported")
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")

	notify := r.Context().Done()

	fs := wrapFrameStore(session.FrameStore)
	if fs == nil {
		writeError(w, http.StatusBadRequest, "frame store not available")
		return
	}

	ch, done := session.LiveStream.Subscribe()
	defer session.LiveStream.Unsubscribe(ch)

	fmt.Fprintf(w, "event: connected\ndata: %s\n\n", session.ID)
	flusher.Flush()

	var lastTs float64 = -1
	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-notify:
			return
		case <-done:
			fmt.Fprintf(w, "event: session_ended\ndata: session %s has ended\n\n", session.ID)
			flusher.Flush()
			return
		case _, ok := <-ch:
			if !ok {
				return
			}
		case <-ticker.C:
			frames := fs.GetAll()
			if len(frames) == 0 {
				continue
			}

			var newFrames []terminal.Frame
			for _, f := range frames {
				if f.Timestamp > lastTs {
					newFrames = append(newFrames, f)
					lastTs = f.Timestamp
				}
			}

			for _, f := range newFrames {
				text := f.RenderText()
				encoded := jsonEscape(text)
				fmt.Fprintf(w, "event: frame\ndata: {\"timestamp\":%f,\"text\":%s}\n\n", f.Timestamp, encoded)
				flusher.Flush()
			}
		}
	}
}

func jsonEscape(s string) string {
	b, _ := json.Marshal(s)
	return string(b)
}

func (s *Server) handleSessionReport(w http.ResponseWriter, r *http.Request, session *models.Session) {
	format := r.URL.Query().Get("format")
	if format == "" {
		format = "json"
	}

	aiResults := session.GetAIResults()
	approvalRecords := session.GetApprovalRecords()

	highestRisk := session.GetHighestRiskLevel()
	blockedCount := session.GetBlockedCount()

	type RiskStat struct {
		Level int `json:"level"`
		Count int `json:"count"`
	}
	riskDistribution := make(map[int]int)
	for _, r := range aiResults {
		riskDistribution[r.RiskLevel]++
	}
	stats := make([]RiskStat, 0, 5)
	for level := 1; level <= 5; level++ {
		stats = append(stats, RiskStat{Level: level, Count: riskDistribution[level]})
	}

	categoryDistribution := make(map[string]int)
	for _, r := range aiResults {
		categoryDistribution[r.Category]++
	}

	report := map[string]interface{}{
		"session_id":           session.ID,
		"user":                 session.User,
		"target_host":          fmt.Sprintf("%s:%d", session.TargetHost, session.TargetPort),
		"duration":             session.Duration,
		"highest_risk_level":   highestRisk,
		"total_commands":       len(session.Commands),
		"ai_analyzed_count":    len(aiResults),
		"approval_count":       len(approvalRecords),
		"blocked_count":        blockedCount,
		"approved_count":       countApprovalsByStatus(approvalRecords, "approved"),
		"rejected_count":       countApprovalsByStatus(approvalRecords, "rejected"),
		"timeout_count":        countApprovalsByStatus(approvalRecords, "timeout"),
		"risk_distribution":    stats,
		"category_distribution": categoryDistribution,
		"ai_results":           aiResults,
		"approval_records":     approvalRecords,
		"pattern_findings":     session.RiskFindings,
	}

	if format == "csv" {
		w.Header().Set("Content-Type", "text/csv")
		w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=session-%s-report.csv", session.ID))
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(buildCSVReport(report)))
		return
	}

	writeSuccess(w, report)
}

func countApprovalsByStatus(records []models.ApprovalRecordItem, status string) int {
	count := 0
	for _, r := range records {
		if r.Status == status {
			count++
		}
	}
	return count
}

func buildCSVReport(report map[string]interface{}) string {
	csv := "session_id,user,target_host,duration,highest_risk,total_commands,ai_analyzed,approvals,blocked,approved,rejected,timeout\n"
	csv += fmt.Sprintf("%s,%s,%s,%.2f,%d,%d,%d,%d,%d,%d,%d,%d\n",
		report["session_id"],
		report["user"],
		report["target_host"],
		report["duration"],
		report["highest_risk_level"],
		report["total_commands"],
		report["ai_analyzed_count"],
		report["approval_count"],
		report["blocked_count"],
		report["approved_count"],
		report["rejected_count"],
		report["timeout_count"],
	)
	return csv
}

func (s *Server) handleReports(w http.ResponseWriter, r *http.Request) {
	sessions := s.store.List()

	format := r.URL.Query().Get("format")
	if format == "csv" {
		w.Header().Set("Content-Type", "text/csv")
		w.Header().Set("Content-Disposition", "attachment; filename=risk-report.csv")
		w.WriteHeader(http.StatusOK)
		csv := "session_id,user,target_host,duration,highest_risk,commands,ai_analyzed,approvals,blocked,approved,rejected,timeout\n"
		for _, session := range sessions {
			csv += fmt.Sprintf("%s,%s,%s:%d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d\n",
				session.ID,
				session.User,
				session.TargetHost,
				session.TargetPort,
				session.Duration,
				session.GetHighestRiskLevel(),
				len(session.Commands),
				len(session.GetAIResults()),
				len(session.GetApprovalRecords()),
				session.GetBlockedCount(),
				countSessionApprovals(session, "approved"),
				countSessionApprovals(session, "rejected"),
				countSessionApprovals(session, "timeout"),
			)
		}
		w.Write([]byte(csv))
		return
	}

	type SessionSummary struct {
		SessionID      string `json:"session_id"`
		User           string `json:"user"`
		TargetHost     string `json:"target_host"`
		Duration       float64 `json:"duration"`
		HighestRisk    int    `json:"highest_risk"`
		Commands       int    `json:"commands"`
		AIResults      int    `json:"ai_results"`
		Approvals      int    `json:"approvals"`
		Blocked        int    `json:"blocked"`
		Approved       int    `json:"approved"`
		Rejected       int    `json:"rejected"`
		Timeout        int    `json:"timeout"`
		Status         string `json:"status"`
	}

	summaries := make([]SessionSummary, 0, len(sessions))
	totalBlocked := 0
	totalApproved := 0
	totalRejected := 0
	totalTimeout := 0

	for _, session := range sessions {
		highest := session.GetHighestRiskLevel()
		blocked := session.GetBlockedCount()
		approved := countSessionApprovals(session, "approved")
		rejected := countSessionApprovals(session, "rejected")
		timeout := countSessionApprovals(session, "timeout")

		totalBlocked += blocked
		totalApproved += approved
		totalRejected += rejected
		totalTimeout += timeout

		summaries = append(summaries, SessionSummary{
			SessionID:   session.ID,
			User:        session.User,
			TargetHost:  fmt.Sprintf("%s:%d", session.TargetHost, session.TargetPort),
			Duration:    session.Duration,
			HighestRisk: highest,
			Commands:    len(session.Commands),
			AIResults:   len(session.GetAIResults()),
			Approvals:   len(session.GetApprovalRecords()),
			Blocked:     blocked,
			Approved:    approved,
			Rejected:    rejected,
			Timeout:     timeout,
			Status:      string(session.Status),
		})
	}

	writeSuccess(w, map[string]interface{}{
		"sessions":      summaries,
		"total_sessions": len(sessions),
		"total_blocked":  totalBlocked,
		"total_approved": totalApproved,
		"total_rejected": totalRejected,
		"total_timeout":  totalTimeout,
	})
}

func countSessionApprovals(session *models.Session, status string) int {
	count := 0
	for _, r := range session.GetApprovalRecords() {
		if r.Status == status {
			count++
		}
	}
	return count
}
