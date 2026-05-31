package assert

import (
	"encoding/json"
	"fmt"
	"reflect"
	"regexp"
	"strconv"
	"strings"

	"github.com/grpctest/grpctest/internal/vars"
)

type Assertion struct {
	Field    string      `json:"field"`
	Operator string      `json:"op"`
	Expected interface{} `json:"expected"`
}

type Result struct {
	Passed   bool
	Field    string
	Operator string
	Actual   interface{}
	Expected interface{}
	Error    string
}

func Run(store *vars.Store, responseMap map[string]interface{}, as []Assertion) []Result {
	results := make([]Result, 0, len(as))
	for _, a := range as {
		exp := store.SubstituteAny(a.Expected)
		actual, ok := getByPath(responseMap, a.Field)
		if !ok && a.Operator != "not_exists" && a.Operator != "exists" {
			results = append(results, Result{
				Passed: false, Field: a.Field, Operator: a.Operator,
				Expected: exp, Error: fmt.Sprintf("field %q not found", a.Field),
			})
			continue
		}
		pass, errMsg := check(actual, a.Operator, exp)
		results = append(results, Result{
			Passed:   pass,
			Field:    a.Field,
			Operator: a.Operator,
			Actual:   actual,
			Expected: exp,
			Error:    errMsg,
		})
	}
	return results
}

func getByPath(m map[string]interface{}, path string) (interface{}, bool) {
	parts := splitPath(path)
	if len(parts) == 0 {
		return nil, false
	}
	var cur interface{} = m
	for _, p := range parts {
		if idx, ok := arrayIdx(p); ok {
			arr, ok2 := cur.([]interface{})
			if !ok2 || idx < 0 || idx >= len(arr) {
				return nil, false
			}
			cur = arr[idx]
			continue
		}
		mm, ok2 := cur.(map[string]interface{})
		if !ok2 {
			return nil, false
		}
		v, ok3 := mm[p]
		if !ok3 {
			return nil, false
		}
		cur = v
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

func arrayIdx(p string) (int, bool) {
	if strings.HasPrefix(p, "[") && strings.HasSuffix(p, "]") {
		n, err := strconv.Atoi(p[1 : len(p)-1])
		if err == nil {
			return n, true
		}
	}
	return 0, false
}

func check(actual interface{}, op string, expected interface{}) (bool, string) {
	switch op {
	case "eq", "equals", "==":
		return equal(actual, expected)
	case "ne", "!=":
		ok, _ := equal(actual, expected)
		if ok {
			return false, fmt.Sprintf("expected not equal to %v but got %v", expected, actual)
		}
		return true, ""
	case "gt", ">":
		return cmp(actual, expected, func(a, b float64) bool { return a > b }, "greater than")
	case "lt", "<":
		return cmp(actual, expected, func(a, b float64) bool { return a < b }, "less than")
	case "gte", ">=":
		return cmp(actual, expected, func(a, b float64) bool { return a >= b }, "greater than or equal")
	case "lte", "<=":
		return cmp(actual, expected, func(a, b float64) bool { return a <= b }, "less than or equal")
	case "contains":
		return containsCheck(actual, expected)
	case "regex":
		return regexMatch(actual, expected)
	case "exists":
		if actual == nil {
			return false, "field does not exist"
		}
		return true, ""
	case "not_exists":
		if actual == nil {
			return true, ""
		}
		return false, "field exists"
	case "len":
		return lengthCheck(actual, expected)
	case "type":
		return typeCheck(actual, expected)
	default:
		return false, fmt.Sprintf("unsupported operator %q", op)
	}
}

func equal(a, b interface{}) (bool, string) {
	an := normalize(a)
	bn := normalize(b)
	if reflect.DeepEqual(an, bn) {
		return true, ""
	}
	return false, fmt.Sprintf("expected %v (%T), got %v (%T)", bn, bn, an, an)
}

func normalize(v interface{}) interface{} {
	switch t := v.(type) {
	case float64, float32, int, int32, int64, uint, uint32, uint64:
		return toFloatMust(v)
	case json.Number:
		f, err := t.Float64()
		if err == nil {
			return f
		}
		return t.String()
	case string, bool, nil:
		return v
	case []interface{}:
		out := make([]interface{}, len(t))
		for i, vv := range t {
			out[i] = normalize(vv)
		}
		return out
	case map[string]interface{}:
		out := make(map[string]interface{}, len(t))
		for k, vv := range t {
			out[k] = normalize(vv)
		}
		return out
	default:
		return v
	}
}

func toFloatMust(v interface{}) float64 {
	f, _ := toFloat(v)
	return f
}

func cmp(actual, expected interface{}, f func(a, b float64) bool, label string) (bool, string) {
	a, ok := toFloat(actual)
	if !ok {
		return false, fmt.Sprintf("cannot convert %v to number", actual)
	}
	b, ok := toFloat(expected)
	if !ok {
		return false, fmt.Sprintf("cannot convert %v to number", expected)
	}
	if f(a, b) {
		return true, ""
	}
	return false, fmt.Sprintf("expected %v %s %v", actual, label, expected)
}

func toFloat(v interface{}) (float64, bool) {
	switch t := v.(type) {
	case float64:
		return t, true
	case float32:
		return float64(t), true
	case int:
		return float64(t), true
	case int32:
		return float64(t), true
	case int64:
		return float64(t), true
	case uint:
		return float64(t), true
	case uint32:
		return float64(t), true
	case uint64:
		return float64(t), true
	case json.Number:
		f, err := t.Float64()
		return f, err == nil
	case string:
		f, err := strconv.ParseFloat(t, 64)
		return f, err == nil
	}
	return 0, false
}

func containsCheck(actual, expected interface{}) (bool, string) {
	expStr := fmt.Sprintf("%v", expected)
	switch t := actual.(type) {
	case string:
		if strings.Contains(t, expStr) {
			return true, ""
		}
		return false, fmt.Sprintf("%q does not contain %q", t, expStr)
	case []interface{}:
		for _, v := range t {
			if fmt.Sprintf("%v", v) == expStr {
				return true, ""
			}
		}
		return false, fmt.Sprintf("array does not contain %v", expected)
	}
	return false, fmt.Sprintf("cannot check contains on %T", actual)
}

func regexMatch(actual, expected interface{}) (bool, string) {
	reStr, ok := expected.(string)
	if !ok {
		return false, "regex expected must be a string"
	}
	re, err := regexp.Compile(reStr)
	if err != nil {
		return false, fmt.Sprintf("invalid regex: %v", err)
	}
	s := fmt.Sprintf("%v", actual)
	if re.MatchString(s) {
		return true, ""
	}
	return false, fmt.Sprintf("%q does not match regex %q", s, reStr)
}

func lengthCheck(actual, expected interface{}) (bool, string) {
	ex, ok := toFloat(expected)
	if !ok {
		return false, "len expected must be a number"
	}
	var l int
	switch t := actual.(type) {
	case string:
		l = len(t)
	case []interface{}:
		l = len(t)
	case map[string]interface{}:
		l = len(t)
	default:
		return false, fmt.Sprintf("cannot compute length of %T", actual)
	}
	if float64(l) == ex {
		return true, ""
	}
	return false, fmt.Sprintf("expected length %v, got %d", expected, l)
}

func typeCheck(actual, expected interface{}) (bool, string) {
	exp, ok := expected.(string)
	if !ok {
		return false, "type expected must be a string"
	}
	got := typeName(actual)
	if got == exp {
		return true, ""
	}
	return false, fmt.Sprintf("expected type %q, got %q", exp, got)
}

func typeName(v interface{}) string {
	if v == nil {
		return "null"
	}
	switch v.(type) {
	case string:
		return "string"
	case bool:
		return "bool"
	case float64, float32, int, int32, int64, uint, uint32, uint64, json.Number:
		return "number"
	case []interface{}:
		return "array"
	case map[string]interface{}:
		return "object"
	}
	return fmt.Sprintf("%T", v)
}
