package model

import "time"

type ThreatLevel int

const (
	LevelUnknown ThreatLevel = 0
	Level1       ThreatLevel = 1
	Level2       ThreatLevel = 2
	Level3       ThreatLevel = 3
	Level4       ThreatLevel = 4
	Level5       ThreatLevel = 5
)

func ValidLevel(l int) bool { return l >= 1 && l <= 5 }

type SourceRecord struct {
	Peer      string      `json:"peer"`
	PeerASN   uint32      `json:"peer_asn"`
	Prefix    string      `json:"prefix"`
	Level     ThreatLevel `json:"level"`
	UpdatedAt time.Time   `json:"updated_at"`
	Raw       string      `json:"raw,omitempty"`
}

type BehaviorFeatures struct {
	RequestCount   int       `json:"request_count"`
	RequestFreq    float64   `json:"request_frequency"`
	FailureRate    float64   `json:"failure_rate"`
	PathEntropy    float64   `json:"path_entropy"`
	UniquePaths    int       `json:"unique_paths"`
	DaytimeRatio   float64   `json:"daytime_ratio"`
	NighttimeRatio float64   `json:"nighttime_ratio"`
	PeakHour       int       `json:"peak_hour"`
	LastSeen       time.Time `json:"last_seen"`
}

type MLScore struct {
	AnomalyScore    float64           `json:"anomaly_score"`
	ReputationScore int               `json:"reputation_score"`
	Features        BehaviorFeatures  `json:"features"`
	ModelUsed       string            `json:"model_used"`
	ScoredAt        time.Time         `json:"scored_at"`
}

type FusionResult struct {
	BGPLevel       ThreatLevel   `json:"bgp_level"`
	BGPWeight      float64       `json:"bgp_weight"`
	MLScore        int           `json:"ml_score"`
	MLWeight       float64       `json:"ml_weight"`
	FusedScore     int           `json:"fused_score"`
	FinalLevel     ThreatLevel   `json:"final_level"`
	Judgment       string        `json:"judgment"`
}

type QueryResult struct {
	QueryIP string        `json:"query_ip"`
	Matched string        `json:"matched_prefix"`
	Level   ThreatLevel   `json:"level"`
	Sources []SourceRecord `json:"sources"`
	ML      *MLScore       `json:"ml,omitempty"`
	Fusion  *FusionResult  `json:"fusion,omitempty"`
}
