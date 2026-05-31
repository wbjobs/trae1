package audit

import (
	"regexp"
	"strings"

	"bastion/internal/config"
	"bastion/internal/models"
)

type Detector struct {
	patterns map[string]*compiledPattern
}

type compiledPattern struct {
	regex    *regexp.Regexp
	severity models.RiskLevel
	category string
}

var defaultPatterns = []struct {
	pattern  string
	severity models.RiskLevel
	category string
}{
	{`rm\s+-[rf]+\s+/`, models.RiskLevelCritical, "filesystem_destruction"},
	{`rm\s+-[rf]+\s+/\*`, models.RiskLevelCritical, "filesystem_destruction"},
	{`rm\s+-[rf]+\s+/\.\*`, models.RiskLevelCritical, "filesystem_destruction"},
	{`rm\s+-rf\s+/`, models.RiskLevelCritical, "filesystem_destruction"},
	{`rm\s+-rf\s+/\*`, models.RiskLevelCritical, "filesystem_destruction"},
	{`mkfs`, models.RiskLevelCritical, "filesystem_operation"},
	{`mkfs\.\w+`, models.RiskLevelCritical, "filesystem_operation"},
	{`dd\s+if=`, models.RiskLevelHigh, "disk_operation"},
	{`dd\s+of=`, models.RiskLevelHigh, "disk_operation"},
	{`>\s*/dev/sd`, models.RiskLevelCritical, "disk_overwrite"},
	{`chmod\s+(-R\s+)?777`, models.RiskLevelHigh, "permission_change"},
	{`chmod\s+(-R\s+)?\+s`, models.RiskLevelMedium, "suid_change"},
	{`chown\s+-R\s+`, models.RiskLevelMedium, "ownership_change"},
	{`chattr\s+\+i`, models.RiskLevelMedium, "immutable_flag"},
	{`passwd`, models.RiskLevelMedium, "password_change"},
	{`userdel`, models.RiskLevelMedium, "user_management"},
	{`groupdel`, models.RiskLevelMedium, "group_management"},
	{`usermod\s+-[a-zA-Z]*d`, models.RiskLevelMedium, "user_modification"},
	{`shutdown`, models.RiskLevelHigh, "system_shutdown"},
	{`reboot`, models.RiskLevelHigh, "system_reboot"},
	{`poweroff`, models.RiskLevelHigh, "system_poweroff"},
	{`halt`, models.RiskLevelHigh, "system_halt"},
	{`init\s+0`, models.RiskLevelHigh, "system_halt"},
	{`init\s+6`, models.RiskLevelHigh, "system_reboot"},
	{`iptables\s+-F`, models.RiskLevelMedium, "firewall_change"},
	{`iptables\s+-X`, models.RiskLevelMedium, "firewall_change"},
	{`systemctl\s+stop\s+sshd`, models.RiskLevelHigh, "service_management"},
	{`systemctl\s+stop\s+ssh`, models.RiskLevelHigh, "service_management"},
	{`kill\s+-9\s+1`, models.RiskLevelCritical, "process_termination"},
	{`killall\s+-9\s+`, models.RiskLevelMedium, "process_termination"},
	{`pkill\s+-9`, models.RiskLevelMedium, "process_termination"},
	{`wget\s+.*\|\s*(sudo\s+)?bash`, models.RiskLevelCritical, "remote_code_execution"},
	{`curl\s+.*\|\s*(sudo\s+)?bash`, models.RiskLevelCritical, "remote_code_execution"},
	{`nc\s+-[le]`, models.RiskLevelMedium, "network_listener"},
	{`ncat\s+-[le]`, models.RiskLevelMedium, "network_listener"},
	{`tcpdump`, models.RiskLevelMedium, "network_capture"},
	{`sudo\s+su`, models.RiskLevelMedium, "privilege_escalation"},
	{`su\s+`, models.RiskLevelLow, "user_switch"},
	{`visudo`, models.RiskLevelHigh, "sudoers_edit"},
	{`ssh-keygen`, models.RiskLevelLow, "key_generation"},
	{`history\s+-c`, models.RiskLevelMedium, "history_clear"},
	{`export\s+HISTFILE=`, models.RiskLevelMedium, "history_disable"},
	{`unset\s+HISTFILE`, models.RiskLevelMedium, "history_disable"},
	{`alias\s+rm=`, models.RiskLevelLow, "command_alias"},
	{`export\s+PATH=.*:.*`, models.RiskLevelLow, "path_modification"},
}

func NewDetector(cfg *config.Config) *Detector {
	d := &Detector{
		patterns: make(map[string]*compiledPattern),
	}

	for _, p := range defaultPatterns {
		re, err := regexp.Compile(`(?i)` + p.pattern)
		if err == nil {
			d.patterns[p.pattern] = &compiledPattern{
				regex:    re,
				severity: p.severity,
				category: p.category,
			}
		}
	}

	for _, p := range cfg.Audit.SensitivePatterns {
		re, err := regexp.Compile(`(?i)` + regexp.QuoteMeta(p))
		if err == nil {
			d.patterns[p] = &compiledPattern{
				regex:    re,
				severity: models.RiskLevelHigh,
				category: "custom_pattern",
			}
		}
	}

	return d
}

func (d *Detector) Analyze(session *models.Session) *models.Session {
	var findings []models.RiskFinding
	highestRisk := models.RiskLevelLow

	for _, cmd := range session.Commands {
		for pattern, cp := range d.patterns {
			if cp.regex.MatchString(cmd.Command) {
				findings = append(findings, models.RiskFinding{
					Command:   cmd.Command,
					Pattern:   pattern,
					Timestamp: cmd.Timestamp,
					Severity:  cp.severity,
				})
				if severityRank(cp.severity) > severityRank(highestRisk) {
					highestRisk = cp.severity
				}
			}
		}
	}

	session.SetRisk(highestRisk, findings)
	return session
}

func severityRank(level models.RiskLevel) int {
	switch level {
	case models.RiskLevelLow:
		return 1
	case models.RiskLevelMedium:
		return 2
	case models.RiskLevelHigh:
		return 3
	case models.RiskLevelCritical:
		return 4
	default:
		return 0
	}
}

func ExtractCommandsFromRecording(recordingPath string) ([]string, error) {
	return nil, nil
}

func (d *Detector) DetectRiskLevel(commands []string) (models.RiskLevel, []string) {
	highest := models.RiskLevelLow
	var flagged []string

	for _, cmd := range commands {
		for pattern, cp := range d.patterns {
			if cp.regex.MatchString(strings.TrimSpace(cmd)) {
				flagged = append(flagged, cmd)
				if severityRank(cp.severity) > severityRank(highest) {
					highest = cp.severity
				}
			}
		}
	}

	return highest, flagged
}
