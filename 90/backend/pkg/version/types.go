package version

import (
	"time"

	"configvcs/pkg/astmerge"
)

type Commit struct {
	ID        string    `json:"id"`
	ParentID  string    `json:"parent_id"`
	RootID    string    `json:"root_id"`
	CID       string    `json:"cid"`
	Message   string    `json:"message"`
	Author    string    `json:"author"`
	Timestamp time.Time `json:"timestamp"`
	Branch    string    `json:"branch"`
}

type Branch struct {
	Name   string `json:"name"`
	Commit string `json:"commit"`
}

type Tag struct {
	Name      string    `json:"name"`
	CommitID  string    `json:"commit_id"`
	Message   string    `json:"message"`
	Timestamp time.Time `json:"timestamp"`
}

type FileDiff struct {
	Added    []DiffLine `json:"added"`
	Removed  []DiffLine `json:"removed"`
	Modified []DiffLine `json:"modified"`
}

type DiffLine struct {
	LineNumber int    `json:"line_number"`
	Content    string `json:"content"`
	Type       string `json:"type"`
}

type MergeResult struct {
	Success      bool                  `json:"success"`
	Conflicts    bool                  `json:"conflicts"`
	MergedCID    string                `json:"merged_cid,omitempty"`
	CommitID     string                `json:"commit_id,omitempty"`
	Message      string                `json:"message"`
	MergeResult  *astmerge.MergeResult `json:"merge_result,omitempty"`
	BaseContent  string                `json:"base_content,omitempty"`
	SourceContent string               `json:"source_content,omitempty"`
	TargetContent string               `json:"target_content,omitempty"`
	BaseCommit   string                `json:"base_commit,omitempty"`
	SourceCommit string                `json:"source_commit,omitempty"`
	TargetCommit string                `json:"target_commit,omitempty"`
}
