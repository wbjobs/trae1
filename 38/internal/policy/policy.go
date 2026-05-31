package policy

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

type Effect string

const (
	EffectAllow Effect = "allow"
	EffectDeny  Effect = "deny"
)

type Policy struct {
	ID          string   `json:"id"`
	Name        string   `json:"name"`
	Description string   `json:"description,omitempty"`
	Source      string   `json:"source"`
	Destination string   `json:"destination"`
	Methods     []string `json:"methods"`
	Path        string   `json:"path"`
	PathType    string   `json:"path_type"`
	Effect      Effect   `json:"effect"`
	Priority    int      `json:"priority"`
	Enabled     bool     `json:"enabled"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`
	regex       *regexp.Regexp
}

type Engine struct {
	mu      sync.RWMutex
	policies []*Policy
	index   map[string]*Policy
}

func NewEngine() *Engine {
	return &Engine{index: make(map[string]*Policy)}
}

func (e *Engine) Set(policies []*Policy) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.policies = make([]*Policy, 0, len(policies))
	e.index = make(map[string]*Policy, len(policies))
	for _, p := range policies {
		cp := p
		if cp.PathType == "regex" && cp.Path != "" {
			if r, err := regexp.Compile(cp.Path); err == nil {
				cp.regex = r
			}
		}
		e.policies = append(e.policies, cp)
		e.index[cp.ID] = cp
	}
	sortByPriority(e.policies)
}

func (e *Engine) All() []*Policy {
	e.mu.RLock()
	defer e.mu.RUnlock()
	out := make([]*Policy, 0, len(e.policies))
	for _, p := range e.policies {
		out = append(out, p)
	}
	return out
}

func (e *Engine) Get(id string) (*Policy, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()
	p, ok := e.index[id]
	return p, ok
}

type Request struct {
	Source      string
	Destination string
	Method      string
	Path        string
}

type Decision struct {
	Allowed    bool
	PolicyID   string
	Reason     string
	Effect     Effect
}

func (e *Engine) Evaluate(req Request) Decision {
	e.mu.RLock()
	policies := e.policies
	e.mu.RUnlock()

	for _, p := range policies {
		if !p.Enabled {
			continue
		}
		if !matchSPIFFE(p.Source, req.Source) {
			continue
		}
		if !matchSPIFFE(p.Destination, req.Destination) {
			continue
		}
		if !matchMethod(p.Methods, req.Method) {
			continue
		}
		if !matchPath(p, req.Path) {
			continue
		}
		if p.Effect == EffectAllow {
			return Decision{Allowed: true, PolicyID: p.ID, Effect: EffectAllow, Reason: "matched policy " + p.Name}
		}
		return Decision{Allowed: false, PolicyID: p.ID, Effect: EffectDeny, Reason: "matched deny policy " + p.Name}
	}
	return Decision{Allowed: false, Reason: "no matching policy; default deny"}
}

func matchSPIFFE(pattern, value string) bool {
	if pattern == "" || pattern == "*" {
		return true
	}
	if pattern == value {
		return true
	}
	return globMatch(pattern, value)
}

func globMatch(pattern, value string) bool {
	var regexStr strings.Builder
	regexStr.WriteByte('^')
	for _, ch := range pattern {
		switch ch {
		case '*':
			regexStr.WriteString(".*")
		case '.', '+', '(', ')', '[', ']', '{', '}', '|', '^', '$', '\\':
			regexStr.WriteByte('\\')
			regexStr.WriteRune(ch)
		default:
			regexStr.WriteRune(ch)
		}
	}
	regexStr.WriteByte('$')
	r, err := regexp.Compile(regexStr.String())
	if err != nil {
		return false
	}
	return r.MatchString(value)
}

func matchMethod(methods []string, m string) bool {
	if len(methods) == 0 {
		return true
	}
	for _, mm := range methods {
		if mm == "*" || mm == m {
			return true
		}
	}
	return false
}

func matchPath(p *Policy, rp string) bool {
	if p.Path == "" || p.Path == "*" {
		return true
	}
	switch p.PathType {
	case "regex":
		if p.regex != nil {
			return p.regex.MatchString(rp)
		}
		return false
	case "prefix":
		return len(rp) >= len(p.Path) && rp[:len(p.Path)] == p.Path
	default:
		return p.Path == rp
	}
}

func sortByPriority(ps []*Policy) {
	sort.Slice(ps, func(i, j int) bool {
		return ps[i].Priority > ps[j].Priority
	})
}

type Store interface {
	List(ctx context.Context) ([]*Policy, error)
	Save(ctx context.Context, p *Policy) error
	Delete(ctx context.Context, id string) error
	Watch(ctx context.Context, onChange func([]*Policy)) error
}

func Marshal(p *Policy) ([]byte, error) {
	return json.Marshal(p)
}

func Unmarshal(b []byte) (*Policy, error) {
	var p Policy
	if err := json.Unmarshal(b, &p); err != nil {
		return nil, err
	}
	return &p, nil
}

func Validate(p *Policy) error {
	if p.ID == "" {
		return fmt.Errorf("policy id required")
	}
	if p.Source == "" {
		return fmt.Errorf("source required")
	}
	if p.Destination == "" {
		return fmt.Errorf("destination required")
	}
	if p.Effect != EffectAllow && p.Effect != EffectDeny {
		return fmt.Errorf("effect must be allow or deny")
	}
	for _, m := range p.Methods {
		switch m {
		case "*", http.MethodGet, http.MethodPost, http.MethodPut, http.MethodDelete,
			http.MethodPatch, http.MethodHead, http.MethodOptions:
		default:
			return fmt.Errorf("invalid method %q", m)
		}
	}
	return nil
}
