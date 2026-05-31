package ml

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"
)

type QueryAnalyzer struct {
	verbose    bool
	partRegexp []*regexp.Regexp
}

func NewQueryAnalyzer(verbose bool) *QueryAnalyzer {
	return &QueryAnalyzer{
		verbose: verbose,
		partRegexp: []*regexp.Regexp{
			regexp.MustCompile(`(?i)(\w+)\s*=\s*'([^']+)'`),
			regexp.MustCompile(`(?i)(\w+)\s*=\s*(\d{4}-\d{2}-\d{2})`),
			regexp.MustCompile(`(?i)partition\s*\(\s*(\w+)\s*\)`),
			regexp.MustCompile(`(?i)dt\s*=\s*'([^']+)'`),
			regexp.MustCompile(`(?i)date\s*=\s*'([^']+)'`),
		},
	}
}

func (a *QueryAnalyzer) ParseSparkEventLog(filePath string) ([]QueryLogEntry, error) {
	if a.verbose {
		fmt.Printf("[ml] parsing Spark event log: %s\n", filePath)
	}

	f, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("open spark log: %w", err)
	}
	defer f.Close()

	var entries []QueryLogEntry
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 10*1024*1024), 100*1024*1024)

	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}

		var event map[string]interface{}
		if err := json.Unmarshal([]byte(line), &event); err != nil {
			continue
		}

		eventType, _ := event["Event"].(string)
		if eventType != "SparkListenerSQLExecutionEnd" {
			continue
		}

		entry := QueryLogEntry{
			Engine: EngineSpark,
			Status: "success",
		}

		if sql, ok := event["sqlExecID"].(float64); ok {
			entry.ID = fmt.Sprintf("spark-%v", sql)
		}

		if props, ok := event["properties"].(map[string]interface{}); ok {
			if sqlText, ok := props["spark.sql.execution.id"].(string); ok {
				entry.QueryText = sqlText
			}
		}

		if ts, ok := event["timestamp"].(float64); ok {
			entry.StartTime = time.UnixMilli(int64(ts))
		}

		entry = a.extractMetadata(entry)
		if entry.TableName != "" {
			entries = append(entries, entry)
		}
	}

	return entries, nil
}

func (a *QueryAnalyzer) ParseTrinoLog(filePath string) ([]QueryLogEntry, error) {
	if a.verbose {
		fmt.Printf("[ml] parsing Trino query log: %s\n", filePath)
	}

	f, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("open trino log: %w", err)
	}
	defer f.Close()

	var entries []QueryLogEntry
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 10*1024*1024), 100*1024*1024)

	tsvRegex := regexp.MustCompile(`\t`)
	tsvHeader := map[string]int{}

	lineNum := 0
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		lineNum++

		fields := tsvRegex.Split(line, -1)

		if lineNum == 1 && strings.Contains(line, "Query") {
			for i, f := range fields {
				tsvHeader[f] = i
			}
			continue
		}

		if len(fields) < 3 {
			continue
		}

		entry := QueryLogEntry{
			Engine: EngineTrino,
			Status: "success",
		}

		if idx, ok := tsvHeader["Query"]; ok && idx < len(fields) {
			entry.ID = fields[idx]
		}
		if idx, ok := tsvHeader["Query_text"]; ok && idx < len(fields) {
			entry.QueryText = fields[idx]
		}
		if idx, ok := tsvHeader["User"]; ok && idx < len(fields) {
			entry.User = fields[idx]
		}
		if idx, ok := tsvHeader["Create_time"]; ok && idx < len(fields) {
			if t, err := time.Parse("2006-01-02T15:04:05.000Z", fields[idx]); err == nil {
				entry.StartTime = t
			} else if t, err := time.Parse("2006-01-02 15:04:05", fields[idx]); err == nil {
				entry.StartTime = t
			}
		}
		if idx, ok := tsvHeader["Execution_time"]; ok && idx < len(fields) {
			if d, err := strconv.ParseInt(fields[idx], 10, 64); err == nil {
				entry.DurationMs = d
			}
		}
		if idx, ok := tsvHeader["Status"]; ok && idx < len(fields) {
			entry.Status = fields[idx]
		}

		entry = a.extractMetadata(entry)
		if entry.TableName != "" {
			entries = append(entries, entry)
		}
	}

	return entries, nil
}

func (a *QueryAnalyzer) ParseGenericLog(filePath string) ([]QueryLogEntry, error) {
	if a.verbose {
		fmt.Printf("[ml] parsing generic query log: %s\n", filePath)
	}

	f, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("open generic log: %w", err)
	}
	defer f.Close()

	var entries []QueryLogEntry
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 10*1024*1024), 100*1024*1024)

	queryStartRegex := regexp.MustCompile(`^(\d{4}-\d{2}-\d{2}T?\d{2}:\d{2}:\d{2}(?:\.\d+)?)`)
	timeFormats := []string{
		"2006-01-02T15:04:05.999999999-07:00",
		"2006-01-02T15:04:05.999Z",
		"2006-01-02 15:04:05.999",
		"2006-01-02 15:04:05",
	}

	var currentEntry *QueryLogEntry
	var currentLines []string

	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}

		if match := queryStartRegex.FindString(line); match != "" {
			if currentEntry != nil && len(currentLines) > 0 {
				currentEntry.QueryText = strings.Join(currentLines, " ")
				entry := a.extractMetadata(*currentEntry)
				if entry.TableName != "" {
					entries = append(entries, entry)
				}
			}

			currentEntry = &QueryLogEntry{
				Engine: EngineGeneric,
				Status: "success",
			}

			for _, format := range timeFormats {
				if t, err := time.Parse(format, match); err == nil {
					currentEntry.StartTime = t
					break
				}
			}

			currentLines = []string{line[len(match):]}
		} else if currentEntry != nil {
			currentLines = append(currentLines, line)
		}
	}

	if currentEntry != nil && len(currentLines) > 0 {
		currentEntry.QueryText = strings.Join(currentLines, " ")
		entry := a.extractMetadata(*currentEntry)
		if entry.TableName != "" {
			entries = append(entries, entry)
		}
	}

	return entries, nil
}

func (a *QueryAnalyzer) ParseJSONLog(filePath string) ([]QueryLogEntry, error) {
	if a.verbose {
		fmt.Printf("[ml] parsing JSON query log: %s\n", filePath)
	}

	f, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("open json log: %w", err)
	}
	defer f.Close()

	var entries []QueryLogEntry
	decoder := json.NewDecoder(f)

	for decoder.More() {
		var entry QueryLogEntry
		if err := decoder.Decode(&entry); err != nil {
			continue
		}
		entry = a.extractMetadata(entry)
		if entry.TableName != "" {
			entries = append(entries, entry)
		}
	}

	return entries, nil
}

func (a *QueryAnalyzer) extractMetadata(entry QueryLogEntry) QueryLogEntry {
	entry.TableName = a.extractTableName(entry.QueryText)
	entry.Partitions = a.extractPartitions(entry.QueryText)
	entry.PartitionValues = a.extractPartitionValues(entry.QueryText)
	entry.BytesScanned = a.estimateBytesScanned(entry.QueryText)
	return entry
}

var tableNameRegex = regexp.MustCompile(`(?i)(?:from|join|into|update|table)\s+(?:IF\s+EXISTS\s+)?([a-zA-Z_][a-zA-Z0-9_]*\.[a-zA-Z_][a-zA-Z0-9_]*|[a-zA-Z_][a-zA-Z0-9_]*)`)

func (a *QueryAnalyzer) extractTableName(query string) string {
	query = removeSQLComments(query)

	matches := tableNameRegex.FindAllStringSubmatch(query, -1)
	if len(matches) > 0 {
		return matches[0][1]
	}

	return ""
}

func (a *QueryAnalyzer) extractPartitions(query string) []string {
	var partitions []string
	seen := map[string]bool{}

	for _, re := range a.partRegexp {
		for _, match := range re.FindAllStringSubmatch(query, -1) {
			partKey := match[1]
			if !seen[partKey] {
				seen[partKey] = true
				partitions = append(partitions, partKey)
			}
		}
	}

	return partitions
}

func (a *QueryAnalyzer) extractPartitionValues(query string) map[string]string {
	result := map[string]string{}

	eqRegex := regexp.MustCompile(`(?i)(\w+)\s*=\s*'([^']+)'`)
	for _, match := range eqRegex.FindAllStringSubmatch(query, -1) {
		result[match[1]] = match[2]
	}

	return result
}

func (a *QueryAnalyzer) estimateBytesScanned(query string) int64 {
	sizeRegex := regexp.MustCompile(`(?i)bytes\s*scanned\s*[:=]\s*(\d+)`)
	matches := sizeRegex.FindAllStringSubmatch(query, -1)
	if len(matches) > 0 {
		if bytes, err := strconv.ParseInt(matches[0][1], 10, 64); err == nil {
			return bytes
		}
	}
	return 0
}

func removeSQLComments(query string) string {
	query = regexp.MustCompile(`(?m)--.*$`).ReplaceAllString(query, "")
	query = regexp.MustCompile(`(?s)/\*.*?\*/`).ReplaceAllString(query, "")
	return query
}

func (a *QueryAnalyzer) AggregateStats(entries []QueryLogEntry) map[string]*TableQueryStats {
	result := map[string]*TableQueryStats{}

	for _, entry := range entries {
		if entry.TableName == "" {
			continue
		}

		stats, ok := result[entry.TableName]
		if !ok {
			stats = &TableQueryStats{
				TableName:  entry.TableName,
				Partitions: map[string]*PartitionQueryStats{},
			}
			result[entry.TableName] = stats
		}

		stats.TotalQueries++
		stats.TotalBytes += entry.BytesScanned

		partValue := "ALL"
		if len(entry.PartitionValues) > 0 {
			var parts []string
			for k, v := range entry.PartitionValues {
				parts = append(parts, fmt.Sprintf("%s=%s", k, v))
			}
			partValue = strings.Join(parts, ",")
		}

		if entry.PartitionName == "" && len(entry.Partitions) > 0 {
			entry.PartitionName = entry.Partitions[0]
		}

		if entry.PartitionName == "" {
			entry.PartitionName = "__no_partition__"
		}

		partKey := entry.PartitionName + "=" + partValue
		partStats, ok := stats.Partitions[partKey]
		if !ok {
			partStats = &PartitionQueryStats{
				TableName:      entry.TableName,
				PartitionName:  entry.PartitionName,
				PartitionValue: partValue,
				FirstQueryTime: entry.StartTime,
			}
			stats.Partitions[partKey] = partStats
		}

		partStats.QueryCount++
		partStats.BytesScanned += entry.BytesScanned
		partStats.TotalDurationMs += entry.DurationMs
		partStats.AvgDurationMs = float64(partStats.TotalDurationMs) / float64(partStats.QueryCount)
		if entry.StartTime.After(partStats.LastQueryTime) {
			partStats.LastQueryTime = entry.StartTime
		}
	}

	return result
}
