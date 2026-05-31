package vars

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
	"sync"
)

type Store struct {
	mu sync.RWMutex
	m  map[string]interface{}
}

func NewStore() *Store {
	return &Store{m: map[string]interface{}{}}
}

func (s *Store) Set(key string, val interface{}) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.m[key] = val
}

func (s *Store) Get(key string) (interface{}, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	v, ok := s.m[key]
	return v, ok
}

func (s *Store) GetByPath(path string) (interface{}, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return getByPathLocked(s.m, path)
}

func getByPathLocked(root map[string]interface{}, path string) (interface{}, bool) {
	parts := splitPath(path)
	if len(parts) == 0 {
		return nil, false
	}
	v, ok := root[parts[0]]
	if !ok {
		return nil, false
	}
	var cur interface{} = v
	for _, p := range parts[1:] {
		cur, ok = indexValue(cur, p)
		if !ok {
			return nil, false
		}
	}
	return cur, true
}

func splitPath(p string) []string {
	var parts []string
	start := 0
	for i := 0; i < len(p); i++ {
		if p[i] == '.' {
			if i > start {
				parts = append(parts, p[start:i])
			}
			start = i + 1
		} else if p[i] == '[' {
			if i > start {
				parts = append(parts, p[start:i])
			}
			end := strings.IndexByte(p[i:], ']')
			if end < 0 {
				return parts
			}
			parts = append(parts, p[i:i+end+1])
			i = i + end
			start = i + 1
		}
	}
	if start < len(p) {
		parts = append(parts, p[start:])
	}
	return parts
}

func indexValue(v interface{}, key string) (interface{}, bool) {
	if strings.HasPrefix(key, "[") && strings.HasSuffix(key, "]") {
		idx, err := strconv.Atoi(key[1 : len(key)-1])
		if err != nil {
			return nil, false
		}
		arr, ok := v.([]interface{})
		if !ok {
			return nil, false
		}
		if idx < 0 || idx >= len(arr) {
			return nil, false
		}
		return arr[idx], true
	}
	m, ok := v.(map[string]interface{})
	if !ok {
		return nil, false
	}
	vv, ok := m[key]
	return vv, ok
}

func (s *Store) Substitute(input string) string {
	out := strings.Builder{}
	i := 0
	for i < len(input) {
		if i+1 < len(input) && input[i] == '$' && input[i+1] == '{' {
			rest := input[i:]
			end := strings.IndexByte(rest, '}')
			if end < 0 {
				out.WriteByte(input[i])
				i++
				continue
			}
			key := rest[2:end]
			v, ok := s.GetByPath(key)
			if ok {
				out.WriteString(formatValue(v))
			} else {
				out.WriteString(rest[:end+1])
			}
			i += end + 1
			continue
		}
		out.WriteByte(input[i])
		i++
	}
	return out.String()
}

func formatValue(v interface{}) string {
	switch t := v.(type) {
	case string:
		return t
	case float64:
		if t == float64(int64(t)) {
			return strconv.FormatInt(int64(t), 10)
		}
		return strconv.FormatFloat(t, 'f', -1, 64)
	case bool:
		return strconv.FormatBool(t)
	case nil:
		return ""
	default:
		b, err := json.Marshal(t)
		if err != nil {
			return fmt.Sprintf("%v", t)
		}
		return string(b)
	}
}

func (s *Store) SubstituteAny(v interface{}) interface{} {
	switch t := v.(type) {
	case string:
		return s.Substitute(t)
	case map[string]interface{}:
		res := make(map[string]interface{}, len(t))
		for k, vv := range t {
			res[k] = s.SubstituteAny(vv)
		}
		return res
	case []interface{}:
		res := make([]interface{}, len(t))
		for i, vv := range t {
			res[i] = s.SubstituteAny(vv)
		}
		return res
	default:
		return v
	}
}
