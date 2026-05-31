package risk

import (
	"crypto/md5"
	"encoding/base64"
	"fmt"
	"math"
	"net/http"
	"strconv"
	"strings"
	"time"
)

type ContextInfo struct {
	IPAddress    string            `json:"ipAddress"`
	Country      string            `json:"country"`
	Region       string            `json:"region"`
	City         string            `json:"city"`
	Timezone     string            `json:"timezone"`
	DeviceFingerprint string       `json:"deviceFingerprint"`
	UserAgent    string            `json:"userAgent"`
	Browser      string            `json:"browser"`
	OS           string            `json:"os"`
	DeviceType   string            `json:"deviceType"`
	LoginTime    time.Time         `json:"loginTime"`
	HourOfDay    int               `json:"hourOfDay"`
	DayOfWeek    int               `json:"dayOfWeek"`
	IsWeekend    bool              `json:"isWeekend"`
	BehaviorScore float64          `json:"behaviorScore"`
	LoginFrequency float64         `json:"loginFrequency"`
	FailedAttempts int             `json:"failedAttempts"`
	Headers      map[string]string `json:"headers"`
}

func CollectContext(r *http.Request, username string, history *UserHistory) *ContextInfo {
	ip := getIPAddress(r)
	ua := r.UserAgent()
	now := time.Now()

	ctx := &ContextInfo{
		IPAddress:   ip,
		UserAgent:   ua,
		LoginTime:   now,
		HourOfDay:   now.Hour(),
		DayOfWeek:   int(now.Weekday()),
		IsWeekend:   now.Weekday() == time.Saturday || now.Weekday() == time.Sunday,
		Headers:     collectHeaders(r),
		Browser:     parseBrowser(ua),
		OS:          parseOS(ua),
		DeviceType:  parseDeviceType(ua),
		DeviceFingerprint: generateDeviceFingerprint(ip, ua),
	}

	if history != nil {
		ctx.Country = history.LastCountry
		ctx.Region = history.LastRegion
		ctx.City = history.LastCity
		ctx.Timezone = history.LastTimezone
		ctx.LoginFrequency = history.LoginFrequency
		ctx.FailedAttempts = history.FailedAttempts
		ctx.BehaviorScore = calculateBehaviorScore(ctx, history)
	}

	return ctx
}

func getIPAddress(r *http.Request) string {
	for _, header := range []string{"X-Forwarded-For", "X-Real-IP", "X-Cluster-Client-Ip"} {
		if ip := r.Header.Get(header); ip != "" {
			ips := strings.Split(ip, ",")
			return strings.TrimSpace(ips[0])
		}
	}
	return r.RemoteAddr
}

func collectHeaders(r *http.Request) map[string]string {
	relevantHeaders := []string{
		"Accept-Language",
		"Accept-Encoding",
		"Accept",
		"Content-Type",
	}
	headers := make(map[string]string)
	for _, h := range relevantHeaders {
		headers[h] = r.Header.Get(h)
	}
	return headers
}

func parseBrowser(userAgent string) string {
	ua := strings.ToLower(userAgent)
	switch {
	case strings.Contains(ua, "edg"):
		return "edge"
	case strings.Contains(ua, "opr") || strings.Contains(ua, "opera"):
		return "opera"
	case strings.Contains(ua, "chrome"):
		return "chrome"
	case strings.Contains(ua, "firefox"):
		return "firefox"
	case strings.Contains(ua, "safari"):
		return "safari"
	default:
		return "unknown"
	}
}

func parseOS(userAgent string) string {
	ua := strings.ToLower(userAgent)
	switch {
	case strings.Contains(ua, "windows"):
		return "windows"
	case strings.Contains(ua, "mac os"):
		return "macos"
	case strings.Contains(ua, "linux"):
		return "linux"
	case strings.Contains(ua, "android"):
		return "android"
	case strings.Contains(ua, "ios") || strings.Contains(ua, "iphone") || strings.Contains(ua, "ipad"):
		return "ios"
	default:
		return "unknown"
	}
}

func parseDeviceType(userAgent string) string {
	ua := strings.ToLower(userAgent)
	switch {
	case strings.Contains(ua, "mobile"):
		return "mobile"
	case strings.Contains(ua, "tablet") || strings.Contains(ua, "ipad"):
		return "tablet"
	default:
		return "desktop"
	}
}

func generateDeviceFingerprint(ip, userAgent string) string {
	hash := md5.Sum([]byte(ip + "|" + userAgent))
	return base64.RawURLEncoding.EncodeToString(hash[:])
}

func (c *ContextInfo) ToFeatures() []float64 {
	ipRisk := ipToRiskScore(c.IPAddress)
	hourRisk := hourToRiskScore(c.HourOfDay)
	weekendRisk := 0.0
	if c.IsWeekend {
		weekendRisk = 1.0
	}
	deviceRisk := deviceToRiskScore(c.DeviceType, c.OS, c.Browser)
	failedRisk := float64(c.FailedAttempts) / 10.0
	behaviorRisk := c.BehaviorScore

	return []float64{
		ipRisk,
		hourRisk,
		weekendRisk,
		deviceRisk,
		failedRisk,
		behaviorRisk,
	}
}

func ipToRiskScore(ip string) float64 {
	parts := strings.Split(ip, ".")
	if len(parts) != 4 {
		return 0.5
	}
	firstOctet, _ := strconv.Atoi(parts[0])
	switch {
	case firstOctet == 10 || firstOctet == 172 || firstOctet == 192:
		return 0.1
	case firstOctet == 127:
		return 0.0
	default:
		return 0.3
	}
}

func hourToRiskScore(hour int) float64 {
	switch {
	case hour >= 1 && hour < 5:
		return 0.8
	case hour >= 22 || hour == 0:
		return 0.5
	case hour >= 9 && hour <= 18:
		return 0.1
	default:
		return 0.3
	}
}

func deviceToRiskScore(deviceType, os, browser string) float64 {
	score := 0.2
	if deviceType == "mobile" {
		score += 0.1
	}
	if os == "unknown" {
		score += 0.3
	}
	if browser == "unknown" {
		score += 0.2
	}
	return math.Min(score, 1.0)
}

func calculateBehaviorScore(ctx *ContextInfo, history *UserHistory) float64 {
	score := 0.0

	if history.LastIP != "" && ctx.IPAddress != history.LastIP {
		score += 0.2
	}
	if history.LastDevice != "" && ctx.DeviceFingerprint != history.LastDevice {
		score += 0.3
	}
	if history.LastHour >= 0 {
		hourDiff := math.Abs(float64(ctx.HourOfDay - history.LastHour))
		if hourDiff > 6 {
			score += 0.2
		}
	}
	if history.DaysSinceLastLogin > 0 {
		score += math.Min(float64(history.DaysSinceLastLogin)/30.0, 0.3)
	}

	return math.Min(score, 1.0)
}

func (c *ContextInfo) String() string {
	return fmt.Sprintf(
		"IP: %s, Country: %s, City: %s, Device: %s, Time: %s, Behavior: %.2f",
		c.IPAddress, c.Country, c.City, c.DeviceType,
		c.LoginTime.Format("2006-01-02 15:04:05"), c.BehaviorScore,
	)
}
