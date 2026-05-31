package version

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/sergi/go-diff/diffmatchpatch"

	"configvcs/pkg/astmerge"
	"configvcs/pkg/ipfs"
	"configvcs/pkg/store"
)

type Service struct {
	store *store.Store
	ipfs  *ipfs.Client
}

func NewService(s *store.Store, ic *ipfs.Client) *Service {
	return &Service{
		store: s,
		ipfs:  ic,
	}
}

func generateCommitID() string {
	h := sha256.New()
	h.Write([]byte(uuid.New().String()))
	return hex.EncodeToString(h.Sum(nil))[:40]
}

func (s *Service) Commit(branch, message, author string, content []byte) (*Commit, error) {
	cid, err := s.ipfs.Add(content)
	if err != nil {
		return nil, fmt.Errorf("ipfs add: %w", err)
	}

	if err := s.ipfs.Pin(cid); err != nil {
		return nil, fmt.Errorf("ipfs pin: %w", err)
	}

	parentID, err := s.store.GetHead(branch)
	if err != nil {
		return nil, fmt.Errorf("get head: %w", err)
	}

	commit := &Commit{
		ID:        generateCommitID(),
		ParentID:  parentID,
		RootID:    cid,
		CID:       cid,
		Message:   message,
		Author:    author,
		Timestamp: time.Now(),
		Branch:    branch,
	}

	if parentID == "" {
		commit.RootID = commit.ID
	} else {
		parent, err := s.store.GetCommit(parentID)
		if err == nil {
			commit.RootID = parent.RootID
		} else {
			commit.RootID = commit.ID
		}
	}

	if err := s.store.SaveCommit(commit); err != nil {
		return nil, fmt.Errorf("save commit: %w", err)
	}

	b, _ := s.store.GetBranch(branch)
	if b == nil {
		b = &Branch{Name: branch, Commit: commit.ID}
	} else {
		b.Commit = commit.ID
	}
	if err := s.store.SaveBranch(b); err != nil {
		return nil, fmt.Errorf("save branch: %w", err)
	}

	return commit, nil
}

func (s *Service) GetCommit(id string) (*Commit, error) {
	return s.store.GetCommit(id)
}

func (s *Service) GetCommitContent(id string) ([]byte, error) {
	c, err := s.store.GetCommit(id)
	if err != nil {
		return nil, err
	}
	return s.ipfs.Get(c.CID)
}

func (s *Service) GetContentByCID(cid string) ([]byte, error) {
	return s.ipfs.Get(cid)
}

func (s *Service) GetContentByTag(tagName string) ([]byte, error) {
	t, err := s.store.GetTag(tagName)
	if err != nil {
		return nil, err
	}
	if t == nil {
		return nil, fmt.Errorf("tag not found: %s", tagName)
	}
	c, err := s.store.GetCommit(t.CommitID)
	if err != nil {
		return nil, err
	}
	return s.ipfs.Get(c.CID)
}

func (s *Service) ListBranches() ([]*Branch, error) {
	return s.store.ListBranches()
}

func (s *Service) GetBranch(name string) (*Branch, error) {
	return s.store.GetBranch(name)
}

func (s *Service) CreateBranch(name, from string) (*Branch, error) {
	existing, _ := s.store.GetBranch(name)
	if existing != nil {
		return nil, fmt.Errorf("branch already exists: %s", name)
	}

	var commitID string
	if from != "" {
		fromBranch, err := s.store.GetBranch(from)
		if err != nil || fromBranch == nil {
			return nil, fmt.Errorf("source branch not found: %s", from)
		}
		commitID = fromBranch.Commit
	} else {
		head, _ := s.store.GetHead("main")
		commitID = head
	}

	b := &Branch{
		Name:   name,
		Commit: commitID,
	}

	if err := s.store.SaveBranch(b); err != nil {
		return nil, err
	}
	if commitID != "" {
		_ = s.dbSetHead(name, commitID)
	}

	return b, nil
}

func (s *Service) dbSetHead(branch, commitID string) error {
	return s.store.SaveCommit(&Commit{
		ID:        generateCommitID(),
		ParentID:  "",
		RootID:    commitID,
		CID:       "",
		Message:   "",
		Author:    "",
		Timestamp: time.Now(),
		Branch:    branch,
	})
}

func (s *Service) DeleteBranch(name string) error {
	if name == "main" {
		return fmt.Errorf("cannot delete main branch")
	}
	b, err := s.store.GetBranch(name)
	if err != nil || b == nil {
		return fmt.Errorf("branch not found: %s", name)
	}
	return s.store.DeleteBranch(name)
}

func (s *Service) ListCommits(branch string) ([]*Commit, error) {
	commits, err := s.store.ListCommitsByBranch(branch)
	if err != nil {
		return nil, err
	}
	sort.Slice(commits, func(i, j int) bool {
		return commits[i].Timestamp.After(commits[j].Timestamp)
	})
	return commits, nil
}

func (s *Service) GetCommitTree(branch string) ([]*Commit, error) {
	commits, err := s.ListCommits(branch)
	if err != nil {
		return nil, err
	}
	return commits, nil
}

func (s *Service) CreateTag(name, commitID, message string) (*Tag, error) {
	existing, _ := s.store.GetTag(name)
	if existing != nil {
		return nil, fmt.Errorf("tag already exists: %s", name)
	}

	c, err := s.store.GetCommit(commitID)
	if err != nil || c == nil {
		return nil, fmt.Errorf("commit not found: %s", commitID)
	}

	t := &Tag{
		Name:      name,
		CommitID:  commitID,
		Message:   message,
		Timestamp: time.Now(),
	}

	if err := s.store.SaveTag(t); err != nil {
		return nil, err
	}

	return t, nil
}

func (s *Service) GetTag(name string) (*Tag, error) {
	return s.store.GetTag(name)
}

func (s *Service) ListTags() ([]*Tag, error) {
	return s.store.ListTags()
}

func (s *Service) DeleteTag(name string) error {
	t, err := s.store.GetTag(name)
	if err != nil || t == nil {
		return fmt.Errorf("tag not found: %s", name)
	}
	return s.store.DeleteTag(name)
}

func (s *Service) Diff(commitA, commitB string) (*FileDiff, error) {
	contentA, err := s.GetCommitContent(commitA)
	if err != nil {
		return nil, fmt.Errorf("get content A: %w", err)
	}

	contentB, err := s.GetCommitContent(commitB)
	if err != nil {
		return nil, fmt.Errorf("get content B: %w", err)
	}

	return s.computeDiff(string(contentA), string(contentB)), nil
}

func (s *Service) DiffWithCID(cid string) (*FileDiff, error) {
	c, err := s.store.GetCommitByCID(cid)
	if err != nil || c == nil {
		return nil, fmt.Errorf("commit not found for CID: %s", cid)
	}

	if c.ParentID == "" {
		return &FileDiff{
			Added:    []DiffLine{},
			Removed:  []DiffLine{},
			Modified: []DiffLine{},
		}, nil
	}

	return s.Diff(c.ParentID, c.ID)
}

func (s *Service) computeDiff(a, b string) *FileDiff {
	dmp := diffmatchpatch.New()
	diffs := dmp.DiffMain(a, b, true)

	result := &FileDiff{
		Added:    []DiffLine{},
		Removed:  []DiffLine{},
		Modified: []DiffLine{},
	}

	lineNumA := 1
	lineNumB := 1

	for _, d := range diffs {
		lines := strings.Split(d.Text, "\n")
		if d.Type == diffmatchpatch.DiffEqual {
			lineNumA += len(lines) - 1
			lineNumB += len(lines) - 1
		} else if d.Type == diffmatchpatch.DiffInsert {
			for _, line := range lines {
				if line != "" {
					result.Added = append(result.Added, DiffLine{
						LineNumber: lineNumB,
						Content:    line,
						Type:       "added",
					})
					lineNumB++
				}
			}
		} else if d.Type == diffmatchpatch.DiffDelete {
			for _, line := range lines {
				if line != "" {
					result.Removed = append(result.Removed, DiffLine{
						LineNumber: lineNumA,
						Content:    line,
						Type:       "removed",
					})
					lineNumA++
				}
			}
		}
	}

	return result
}

func (s *Service) Merge(sourceBranch, targetBranch, author, message string, strategy *astmerge.MergeStrategy) (*MergeResult, error) {
	sourceBranchObj, err := s.store.GetBranch(sourceBranch)
	if err != nil || sourceBranchObj == nil {
		return &MergeResult{Success: false, Message: "source branch not found"}, nil
	}

	targetBranchObj, err := s.store.GetBranch(targetBranch)
	if err != nil || targetBranchObj == nil {
		return &MergeResult{Success: false, Message: "target branch not found"}, nil
	}

	sourceCommit, err := s.store.GetCommit(sourceBranchObj.Commit)
	if err != nil {
		return &MergeResult{Success: false, Message: "source commit not found"}, nil
	}

	targetCommit, err := s.store.GetCommit(targetBranchObj.Commit)
	if err != nil {
		return &MergeResult{Success: false, Message: "target commit not found"}, nil
	}

	baseCommit, err := s.findCommonAncestor(sourceCommit, targetCommit)
	if err != nil || baseCommit == nil {
		return s.fastForwardMerge(sourceCommit, targetBranch, author, message)
	}

	baseContent, err := s.ipfs.Get(baseCommit.CID)
	if err != nil {
		return &MergeResult{Success: false, Message: "cannot get base content"}, nil
	}

	sourceContent, err := s.ipfs.Get(sourceCommit.CID)
	if err != nil {
		return &MergeResult{Success: false, Message: "cannot get source content"}, nil
	}

	targetContent, err := s.ipfs.Get(targetCommit.CID)
	if err != nil {
		return &MergeResult{Success: false, Message: "cannot get target content"}, nil
	}

	mergeResult, err := astmerge.ThreeWayMerge(string(baseContent), string(sourceContent), string(targetContent), strategy)
	if err != nil {
		return &MergeResult{
			Success: false,
			Message: fmt.Sprintf("ast merge failed: %v", err),
		}, nil
	}

	if mergeResult.HasConflicts {
		return &MergeResult{
			Success:       false,
			Conflicts:     true,
			Message:       fmt.Sprintf("merge conflicts detected: %d conflict(s) found, manual resolution required", mergeResult.ConflictCount),
			MergeResult:   mergeResult,
			BaseContent:   string(baseContent),
			SourceContent: string(sourceContent),
			TargetContent: string(targetContent),
			BaseCommit:    baseCommit.ID,
			SourceCommit:  sourceCommit.ID,
			TargetCommit:  targetCommit.ID,
		}, nil
	}

	mergedContent := mergeResult.YAML
	if mergedContent == "" {
		mergedContent = mergeResult.JSON
	}

	mergeCommit, err := s.Commit(targetBranch, message, author, []byte(mergedContent))
	if err != nil {
		return &MergeResult{Success: false, Message: fmt.Sprintf("merge commit failed: %v", err)}, nil
	}

	return &MergeResult{
		Success:       true,
		Conflicts:     false,
		MergedCID:     mergeCommit.CID,
		CommitID:      mergeCommit.ID,
		Message:       fmt.Sprintf("merged %s into %s", sourceBranch, targetBranch),
		MergeResult:   mergeResult,
		BaseContent:   string(baseContent),
		SourceContent: string(sourceContent),
		TargetContent: string(targetContent),
		BaseCommit:    baseCommit.ID,
		SourceCommit:  sourceCommit.ID,
		TargetCommit:  targetCommit.ID,
	}, nil
}

func (s *Service) fastForwardMerge(source *Commit, targetBranch, author, message string) (*MergeResult, error) {
	content, err := s.ipfs.Get(source.CID)
	if err != nil {
		return &MergeResult{Success: false, Message: "cannot get source content"}, nil
	}

	commit, err := s.Commit(targetBranch, message, author, content)
	if err != nil {
		return &MergeResult{Success: false, Message: fmt.Sprintf("commit failed: %v", err)}, nil
	}

	return &MergeResult{
		Success:   true,
		Conflicts: false,
		MergedCID: commit.CID,
		CommitID:  commit.ID,
		Message:   "fast-forward merge completed",
	}, nil
}

func (s *Service) findCommonAncestor(a, b *Commit) (*Commit, error) {
	ancestorsA := s.collectAncestors(a)
	ancestorsB := s.collectAncestorsSet(b)

	for _, id := range ancestorsA {
		if _, ok := ancestorsB[id]; ok {
			ancestor, err := s.store.GetCommit(id)
			if err != nil {
				continue
			}
			return ancestor, nil
		}
	}

	return nil, nil
}

func (s *Service) collectAncestors(c *Commit) []string {
	var ids []string
	current := c
	visited := make(map[string]bool)

	for current != nil {
		if visited[current.ID] {
			break
		}
		visited[current.ID] = true
		ids = append(ids, current.ID)

		if current.ParentID == "" {
			break
		}
		var err error
		current, err = s.store.GetCommit(current.ParentID)
		if err != nil {
			break
		}
	}

	return ids
}

func (s *Service) collectAncestorsSet(c *Commit) map[string]bool {
	set := make(map[string]bool)
	for _, id := range s.collectAncestors(c) {
		set[id] = true
	}
	return set
}

func (s *Service) threeWayMerge(base, source, target string) (string, bool) {
	dmp := diffmatchpatch.New()

	baseLines := strings.Split(base, "\n")
	sourceLines := strings.Split(source, "\n")
	targetLines := strings.Split(target, "\n")

	merged := []string{}
	hasConflicts := false

	bsDiff := dmp.DiffMain(base, source, false)
	btDiff := dmp.DiffMain(base, target, false)

	sourceChunks := s.chunkDiff(bsDiff, baseLines)
	targetChunks := s.chunkDiff(btDiff, baseLines)

	i := 0
	baseIdx := 0

	for i < len(baseLines) {
		inSource := -1
		inTarget := -1

		for idx, chunk := range sourceChunks {
			if chunk.BaseStart <= baseIdx && baseIdx < chunk.BaseEnd {
				inSource = idx
				break
			}
		}

		for idx, chunk := range targetChunks {
			if chunk.BaseStart <= baseIdx && baseIdx < chunk.BaseEnd {
				inTarget = idx
				break
			}
		}

		if inSource >= 0 && inTarget >= 0 {
			if strings.Join(sourceChunks[inSource].Lines, "\n") == strings.Join(targetChunks[inTarget].Lines, "\n") {
				merged = append(merged, sourceChunks[inSource].Lines...)
			} else {
				hasConflicts = true
				merged = append(merged, "<<<<<<< source")
				merged = append(merged, sourceChunks[inSource].Lines...)
				merged = append(merged, "=======")
				merged = append(merged, targetChunks[inTarget].Lines...)
				merged = append(merged, ">>>>>>> target")
			}
			baseIdx = max(sourceChunks[inSource].BaseEnd, targetChunks[inTarget].BaseEnd)
			i = baseIdx
		} else if inSource >= 0 {
			merged = append(merged, sourceChunks[inSource].Lines...)
			baseIdx = sourceChunks[inSource].BaseEnd
			i = baseIdx
		} else if inTarget >= 0 {
			merged = append(merged, targetChunks[inTarget].Lines...)
			baseIdx = targetChunks[inTarget].BaseEnd
			i = baseIdx
		} else {
			if baseIdx < len(baseLines) {
				merged = append(merged, baseLines[baseIdx])
			}
			baseIdx++
			i = baseIdx
		}
	}

	return strings.Join(merged, "\n"), hasConflicts
}

type diffChunk struct {
	BaseStart int
	BaseEnd   int
	Lines     []string
}

func (s *Service) chunkDiff(diffs []diffmatchpatch.Diff, baseLines []string) []diffChunk {
	var chunks []diffChunk
	baseIdx := 0

	for _, d := range diffs {
		if d.Type == diffmatchpatch.DiffEqual {
			baseIdx += len(strings.Split(d.Text, "\n")) - 1
		} else if d.Type == diffmatchpatch.DiffInsert {
			start := baseIdx
			lines := strings.Split(d.Text, "\n")
			if lines[len(lines)-1] == "" {
				lines = lines[:len(lines)-1]
			}
			chunks = append(chunks, diffChunk{
				BaseStart: start,
				BaseEnd:   start,
				Lines:     lines,
			})
		} else if d.Type == diffmatchpatch.DiffDelete {
			start := baseIdx
			delLines := strings.Split(d.Text, "\n")
			baseIdx += len(delLines) - 1
			if delLines[len(delLines)-1] == "" {
				delLines = delLines[:len(delLines)-1]
			}
			chunks = append(chunks, diffChunk{
				BaseStart: start,
				BaseEnd:   baseIdx,
				Lines:     []string{},
			})
		}
	}

	return chunks
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func (s *Service) ASTDiff(commitA, commitB string) (*astmerge.DiffResult, error) {
	contentA, err := s.GetCommitContent(commitA)
	if err != nil {
		return nil, fmt.Errorf("get content A: %w", err)
	}

	contentB, err := s.GetCommitContent(commitB)
	if err != nil {
		return nil, fmt.Errorf("get content B: %w", err)
	}

	nodeA, _, err := astmerge.ParseAuto(string(contentA))
	if err != nil {
		return nil, fmt.Errorf("parse A: %w", err)
	}

	nodeB, _, err := astmerge.ParseAuto(string(contentB))
	if err != nil {
		return nil, fmt.Errorf("parse B: %w", err)
	}

	return astmerge.Diff(nodeA, nodeB), nil
}

type ResolveConflictRequest struct {
	MergeResult *astmerge.MergeResult `json:"merge_result"`
	Resolutions   map[string]string        `json:"resolutions"`
	TargetBranch  string                    `json:"target_branch"`
	Message       string                    `json:"message"`
	Author        string                    `json:"author"`
}

func (s *Service) ResolveAndCommit(req *ResolveConflictRequest) (*MergeResult, error) {
	if req.MergeResult == nil || req.MergeResult.Root == nil {
		return &MergeResult{Success: false, Message: "invalid merge result"}, nil
	}

	rootCopy := req.MergeResult.Root.Copy()

	allResolved, err := astmerge.ResolveConflicts(rootCopy, req.Resolutions)
	if err != nil {
		return &MergeResult{Success: false, Message: fmt.Sprintf("resolve failed: %v", err)}, nil
	}

	if !allResolved {
		remaining := rootCopy.GetAllConflicts()
		return &MergeResult{
			Success:   false,
			Conflicts: true,
			Message:   fmt.Sprintf("%d conflict(s) still unresolved", len(remaining)),
		}, nil
	}

	yamlStr, err := rootCopy.ToYAML()
	if err != nil {
		return &MergeResult{Success: false, Message: fmt.Sprintf("generate yaml failed: %v", err)}, nil
	}

	commit, err := s.Commit(req.TargetBranch, req.Message, req.Author, []byte(yamlStr))
	if err != nil {
		return &MergeResult{Success: false, Message: fmt.Sprintf("commit failed: %v", err)}, nil
	}

	updatedResult := &astmerge.MergeResult{
		Root:         rootCopy,
		HasConflicts: false,
		YAML:         yamlStr,
	}

	return &MergeResult{
		Success:     true,
		Conflicts: false,
		MergedCID: commit.CID,
		CommitID:  commit.ID,
		Message:   "conflicts resolved and committed",
		MergeResult: updatedResult,
	}, nil
}

func (s *Service) PreviewMerge(base, source, target string, strategy *astmerge.MergeStrategy) (*MergeResult, error) {
	baseContent, err := s.ipfs.Get(base)
	if err != nil {
		baseCommit, err2 := s.store.GetCommit(base)
		if err2 != nil {
			return &MergeResult{Success: false, Message: "cannot get base content"}, nil
		}
		baseContent, err = s.ipfs.Get(baseCommit.CID)
		if err != nil {
			return &MergeResult{Success: false, Message: "cannot get base content"}, nil
		}
	}

	sourceContent, err := s.ipfs.Get(source)
	if err != nil {
		sourceCommit, err2 := s.store.GetCommit(source)
		if err2 != nil {
			return &MergeResult{Success: false, Message: "cannot get source content"}, nil
		}
		sourceContent, err = s.ipfs.Get(sourceCommit.CID)
		if err != nil {
			return &MergeResult{Success: false, Message: "cannot get source content"}, nil
		}
	}

	targetContent, err := s.ipfs.Get(target)
	if err != nil {
		targetCommit, err2 := s.store.GetCommit(target)
		if err2 != nil {
			return &MergeResult{Success: false, Message: "cannot get target content"}, nil
		}
		targetContent, err = s.ipfs.Get(targetCommit.CID)
		if err != nil {
			return &MergeResult{Success: false, Message: "cannot get target content"}, nil
		}
	}

	mergeResult, err := astmerge.ThreeWayMerge(string(baseContent), string(sourceContent), string(targetContent), strategy)
	if err != nil {
		return &MergeResult{
			Success: false,
			Message: fmt.Sprintf("ast merge failed: %v", err),
		}, nil
	}

	return &MergeResult{
		Success:       !mergeResult.HasConflicts,
		Conflicts:     mergeResult.HasConflicts,
		Message:       fmt.Sprintf("merge preview: %d conflict(s)", mergeResult.ConflictCount),
		MergeResult:   mergeResult,
		BaseContent:   string(baseContent),
		SourceContent: string(sourceContent),
		TargetContent: string(targetContent),
		BaseCommit:    base,
		SourceCommit:  source,
		TargetCommit:  target,
	}, nil
}

func (s *Service) GetDefaultMergeStrategy() *astmerge.MergeStrategy {
	return astmerge.DefaultMergeStrategy()
}
