package diff

import (
	"encoding/json"
	"fmt"
	"strings"
)

type Change struct {
	Path   string      `json:"path" yaml:"path"`
	Change string      `json:"change" yaml:"change"` // "added" | "removed" | "modified" | "type_changed"
	From   interface{} `json:"from,omitempty" yaml:"from,omitempty"`
	To     interface{} `json:"to,omitempty" yaml:"to,omitempty"`
}

type Report struct {
	Changes    []Change `json:"changes" yaml:"changes"`
	Ignored    []string `json:"ignored,omitempty" yaml:"ignored,omitempty"`
	Identical  bool     `json:"identical" yaml:"identical"`
}

func Diff(expected, actual interface{}, ignores []string) Report {
	ignoreSet := map[string]bool{}
	for _, p := range ignores {
		ignoreSet[p] = true
	}
	rep := Report{Identical: true}
	walk("", expected, actual, ignoreSet, &rep)
	return rep
}

func matchIgnore(p string, ignoreSet map[string]bool) bool {
	if ignoreSet[p] {
		return true
	}
	for pat := range ignoreSet {
		if glob(pat, p) {
			return true
		}
	}
	return false
}

func glob(pattern, path string) bool {
	if strings.HasSuffix(pattern, ".*") {
		prefix := pattern[:len(pattern)-1]
		if strings.HasPrefix(path, prefix) {
			return true
		}
	}
	if strings.HasSuffix(pattern, "[*]") {
		prefix := pattern[:len(pattern)-3]
		rest := path[len(prefix):]
		if strings.HasPrefix(path, prefix) && len(rest) > 0 && rest[0] == '[' {
			return true
		}
	}
	return false
}

func walk(path string, a, b interface{}, ignoreSet map[string]bool, rep *Report) {
	if matchIgnore(path, ignoreSet) {
		rep.Ignored = append(rep.Ignored, path)
		return
	}
	if a == nil && b == nil {
		return
	}
	if a == nil || b == nil {
		rep.Changes = append(rep.Changes, Change{
			Path:   path,
			Change: changeForNil(a, b),
			From:   a,
			To:     b,
		})
		rep.Identical = false
		return
	}
	if isMap(a) && isMap(b) {
		am := a.(map[string]interface{})
		bm := b.(map[string]interface{})
		for k := range am {
			p := join(path, k)
			if _, ok := bm[k]; !ok {
				if matchIgnore(p, ignoreSet) {
					rep.Ignored = append(rep.Ignored, p)
					continue
				}
				rep.Changes = append(rep.Changes, Change{
					Path:   p,
					Change: "removed",
					From:   am[k],
				})
				rep.Identical = false
				continue
			}
			walk(p, am[k], bm[k], ignoreSet, rep)
		}
		for k := range bm {
			if _, ok := am[k]; ok {
				continue
			}
			p := join(path, k)
			if matchIgnore(p, ignoreSet) {
				rep.Ignored = append(rep.Ignored, p)
				continue
			}
			rep.Changes = append(rep.Changes, Change{
				Path:   p,
				Change: "added",
				To:     bm[k],
			})
			rep.Identical = false
		}
		return
	}
	if isSlice(a) && isSlice(b) {
		as := a.([]interface{})
		bs := b.([]interface{})
		max := len(as)
		if len(bs) > max {
			max = len(bs)
		}
		for i := 0; i < max; i++ {
			p := fmt.Sprintf("%s[%d]", path, i)
			if i >= len(as) {
				if matchIgnore(p, ignoreSet) {
					rep.Ignored = append(rep.Ignored, p)
					continue
				}
				rep.Changes = append(rep.Changes, Change{
					Path:   p,
					Change: "added",
					To:     bs[i],
				})
				rep.Identical = false
				continue
			}
			if i >= len(bs) {
				if matchIgnore(p, ignoreSet) {
					rep.Ignored = append(rep.Ignored, p)
					continue
				}
				rep.Changes = append(rep.Changes, Change{
					Path:   p,
					Change: "removed",
					From:   as[i],
				})
				rep.Identical = false
				continue
			}
			walk(p, as[i], bs[i], ignoreSet, rep)
		}
		return
	}
	if !equalValues(a, b) {
		if matchIgnore(path, ignoreSet) {
			rep.Ignored = append(rep.Ignored, path)
			return
		}
		change := "modified"
		if typeName(a) != typeName(b) {
			change = "type_changed"
		}
		rep.Changes = append(rep.Changes, Change{
			Path:   path,
			Change: change,
			From:   a,
			To:     b,
		})
		rep.Identical = false
	}
}

func isMap(v interface{}) bool {
	_, ok := v.(map[string]interface{})
	return ok
}

func isSlice(v interface{}) bool {
	_, ok := v.([]interface{})
	return ok
}

func equalValues(a, b interface{}) bool {
	ab, _ := json.Marshal(a)
	bb, _ := json.Marshal(b)
	return string(ab) == string(bb)
}

func typeName(v interface{}) string {
	switch v.(type) {
	case nil:
		return "null"
	case bool:
		return "bool"
	case string:
		return "string"
	case float64:
		return "number"
	case map[string]interface{}:
		return "object"
	case []interface{}:
		return "array"
	}
	return fmt.Sprintf("%T", v)
}

func changeForNil(a, b interface{}) string {
	if a == nil && b != nil {
		return "added"
	}
	if a != nil && b == nil {
		return "removed"
	}
	return "modified"
}

func join(a, b string) string {
	if a == "" {
		return b
	}
	return a + "." + b
}

func FormatUnified(r Report) string {
	var b strings.Builder
	for _, c := range r.Changes {
		switch c.Change {
		case "added":
			fmt.Fprintf(&b, "+ %s = %v\n", c.Path, c.To)
		case "removed":
			fmt.Fprintf(&b, "- %s = %v\n", c.Path, c.From)
		case "modified", "type_changed":
			fmt.Fprintf(&b, "- %s = %v\n", c.Path, c.From)
			fmt.Fprintf(&b, "+ %s = %v\n", c.Path, c.To)
		}
	}
	if len(r.Ignored) > 0 {
		b.WriteString("# ignored fields:\n")
		for _, p := range r.Ignored {
			fmt.Fprintf(&b, "#   %s\n", p)
		}
	}
	return b.String()
}
