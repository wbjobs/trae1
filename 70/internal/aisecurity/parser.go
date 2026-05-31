package aisecurity

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

func parseAssessment(response string) (*SecurityAssessment, error) {
	response = strings.TrimSpace(response)

	jsonStr := extractJSON(response)
	if jsonStr == "" {
		return parseFallback(response)
	}

	var assessment SecurityAssessment
	if err := json.Unmarshal([]byte(jsonStr), &assessment); err != nil {
		return parseFallback(response)
	}

	if assessment.Score == 0 && response != "" {
		return parseFallback(response)
	}

	normalizeAssessment(&assessment)
	return &assessment, nil
}

func extractJSON(response string) string {
	response = strings.TrimSpace(response)

	jsonBlockRe := regexp.MustCompile("```(?:json)?\\s*\\n?([\\s\\S]*?)\\n?```")
	if matches := jsonBlockRe.FindStringSubmatch(response); len(matches) > 1 {
		return strings.TrimSpace(matches[1])
	}

	start := strings.Index(response, "{")
	end := strings.LastIndex(response, "}")
	if start >= 0 && end > start {
		return response[start : end+1]
	}

	return ""
}

func parseFallback(response string) (*SecurityAssessment, error) {
	assessment := &SecurityAssessment{
		Score:   50,
		Reasons: []string{},
	}

	lower := strings.ToLower(response)

	scoreRe := regexp.MustCompile(`(?i)(?:score|评分|分数)[:\s]*(\d+)`)
	if matches := scoreRe.FindStringSubmatch(response); len(matches) > 1 {
		if score, err := strconv.Atoi(matches[1]); err == nil {
			assessment.Score = score
		}
	}

	if strings.Contains(lower, "critical") || strings.Contains(lower, "严重") ||
		strings.Contains(lower, "dangerous") || strings.Contains(lower, "危险") {
		assessment.Category = "dangerous"
		if assessment.Score == 50 {
			assessment.Score = 20
		}
	} else if strings.Contains(lower, "suspicious") || strings.Contains(lower, "可疑") ||
		strings.Contains(lower, "warning") || strings.Contains(lower, "警告") {
		assessment.Category = "suspicious"
		if assessment.Score == 50 {
			assessment.Score = 65
		}
	} else if strings.Contains(lower, "safe") || strings.Contains(lower, "安全") ||
		strings.Contains(lower, "benign") {
		assessment.Category = "safe"
		if assessment.Score == 50 {
			assessment.Score = 95
		}
	}

	reasonLines := extractReasons(response)
	if len(reasonLines) > 0 {
		assessment.Reasons = reasonLines
	}

	if assessment.Score == 50 && len(assessment.Reasons) == 0 {
		assessment.Reasons = append(assessment.Reasons, "AI assessment unclear, defaulting to cautious score")
		assessment.Category = "suspicious"
	}

	normalizeAssessment(assessment)
	return assessment, nil
}

func extractReasons(response string) []string {
	var reasons []string

	lower := strings.ToLower(response)
	reasonKeywords := []string{"reason", "原因", "issue", "问题", "risk", "风险", "warning", "警告", "concern", "担忧"}

	for _, kw := range reasonKeywords {
		idx := strings.Index(lower, kw)
		if idx >= 0 {
			after := response[idx:]
			lines := strings.SplitN(after, "\n", 3)
			for _, line := range lines {
				line = strings.TrimSpace(line)
				if len(line) > 10 && len(line) < 500 {
					reasons = append(reasons, truncateReason(line))
				}
			}
			if len(reasons) > 0 {
				break
			}
		}
	}

	bulletRe := regexp.MustCompile(`[-*•]\s+(.+?)(?:\n|$)`)
	if matches := bulletRe.FindAllStringSubmatch(response, -1); len(matches) > 0 {
		for _, m := range matches {
			reason := strings.TrimSpace(m[1])
			if len(reason) > 5 && len(reason) < 500 {
				reasons = append(reasons, truncateReason(reason))
			}
		}
	}

	return reasons
}

func truncateReason(reason string) string {
	if len(reason) > 200 {
		return reason[:197] + "..."
	}
	return reason
}

func normalizeAssessment(a *SecurityAssessment) {
	if a.Score < 0 {
		a.Score = 0
	}
	if a.Score > 100 {
		a.Score = 100
	}

	if a.Category == "" {
		switch {
		case a.Score >= 90:
			a.Category = "safe"
		case a.Score >= 60:
			a.Category = "suspicious"
		default:
			a.Category = "dangerous"
		}
	}

	if len(a.Reasons) == 0 {
		switch {
		case a.Score >= 90:
			a.Reasons = []string{"Command appears safe"}
		case a.Score >= 60:
			a.Reasons = []string{"Command requires review"}
		default:
			a.Reasons = []string{fmt.Sprintf("Command scored %d/100, considered high risk", a.Score)}
		}
	}
}
