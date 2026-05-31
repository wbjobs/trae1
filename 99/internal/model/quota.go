package model

import "time"

type Tenant struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	ExportPath  string    `json:"export_path"`
	CapacityBytes int64   `json:"capacity_bytes"`
	FileCount   int64     `json:"file_count"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`
}

type Usage struct {
	TenantID     string    `json:"tenant_id"`
	UsedBytes    int64     `json:"used_bytes"`
	UsedFiles    int64     `json:"used_files"`
	LastScanTime time.Time `json:"last_scan_time"`
}

type AlertEvent struct {
	TenantID   string    `json:"tenant_id"`
	Type       string    `json:"type"`
	Message    string    `json:"message"`
	UsedBytes  int64     `json:"used_bytes"`
	LimitBytes int64     `json:"limit_bytes"`
	UsedFiles  int64     `json:"used_files"`
	LimitFiles int64     `json:"limit_files"`
	Ratio      float64   `json:"ratio"`
	Time       time.Time `json:"time"`
}

type LiftRequest struct {
	ID           string    `json:"id"`
	TenantID     string    `json:"tenant_id"`
	ExtraBytes   int64     `json:"extra_bytes"`
	ExtraFiles   int64     `json:"extra_files"`
	Reason       string    `json:"reason"`
	Approved     bool      `json:"approved"`
	ApprovedBy   string    `json:"approved_by,omitempty"`
	ApprovedAt   time.Time `json:"approved_at,omitempty"`
	StartAt      time.Time `json:"start_at,omitempty"`
	ExpireAt     time.Time `json:"expire_at,omitempty"`
	Restored     bool      `json:"restored"`
	CreatedAt    time.Time `json:"created_at"`
}

type EffectiveQuota struct {
	CapacityBytes int64 `json:"capacity_bytes"`
	FileCount     int64 `json:"file_count"`
}

type MigrationStatus string

const (
	MigrationPending   MigrationStatus = "pending"
	MigrationRunning   MigrationStatus = "running"
	MigrationPaused    MigrationStatus = "paused"
	MigrationSwitching MigrationStatus = "switching"
	MigrationVerifying MigrationStatus = "verifying"
	MigrationCompleted MigrationStatus = "completed"
	MigrationFailed    MigrationStatus = "failed"
	MigrationRollback  MigrationStatus = "rollback"
)

type Migration struct {
	ID           string          `json:"id"`
	TenantID     string          `json:"tenant_id"`
	SourcePath   string          `json:"source_path"`
	TargetPath   string          `json:"target_path"`
	Strategy     string          `json:"strategy"`
	Status       MigrationStatus `json:"status"`
	Progress     int             `json:"progress"`
	FilesTotal   int64           `json:"files_total"`
	FilesCopied  int64           `json:"files_copied"`
	BytesTotal   int64           `json:"bytes_total"`
	BytesCopied  int64           `json:"bytes_copied"`
	Error        string          `json:"error,omitempty"`
	StartedAt    time.Time       `json:"started_at,omitempty"`
	FinishedAt   time.Time       `json:"finished_at,omitempty"`
	PausedAt     time.Time       `json:"paused_at,omitempty"`
	RolledBack   bool            `json:"rolled_back"`
	CreatedAt    time.Time       `json:"created_at"`
}
