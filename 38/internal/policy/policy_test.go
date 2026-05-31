package policy

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestMatchSPIFFE(t *testing.T) {
	tests := []struct {
		pattern string
		value   string
		want    bool
	}{
		{"", "spiffe://example.org/ns/svc/a", true},
		{"*", "spiffe://example.org/ns/svc/a", true},
		{"spiffe://example.org/ns/svc/a", "spiffe://example.org/ns/svc/a", true},
		{"spiffe://example.org/ns/svc/a", "spiffe://example.org/ns/svc/b", false},
		{"spiffe://example.org/ns/*", "spiffe://example.org/ns/svc/a", true},
		{"spiffe://example.org/ns/*", "spiffe://example.org/ns/svc/a/b", true},
		{"spiffe://example.org/*/a", "spiffe://example.org/ns/a", true},
		{"spiffe://example.org/*/a", "spiffe://example.org/ns/b", false},
		{"spiffe://*.org/ns/a", "spiffe://example.org/ns/a", true},
		{"spiffe://*.org/ns/a", "spiffe://other.com/ns/a", false},
	}
	for _, tt := range tests {
		t.Run(tt.pattern+"_"+tt.value, func(t *testing.T) {
			got := matchSPIFFE(tt.pattern, tt.value)
			assert.Equal(t, tt.want, got)
		})
	}
}

func TestMatchMethod(t *testing.T) {
	assert.True(t, matchMethod([]string{"GET"}, "GET"))
	assert.True(t, matchMethod([]string{"*"}, "DELETE"))
	assert.True(t, matchMethod([]string{"GET", "POST"}, "POST"))
	assert.False(t, matchMethod([]string{"GET"}, "POST"))
	assert.True(t, matchMethod([]string{}, "GET"))
}

func TestMatchPath(t *testing.T) {
	p := &Policy{Path: "/api/v1/users", PathType: "exact"}
	assert.True(t, matchPath(p, "/api/v1/users"))
	assert.False(t, matchPath(p, "/api/v1/users/123"))

	p2 := &Policy{Path: "/api/v1/", PathType: "prefix"}
	assert.True(t, matchPath(p2, "/api/v1/users"))
	assert.True(t, matchPath(p2, "/api/v1/orders/456"))
	assert.False(t, matchPath(p2, "/api/v2/users"))

	p3 := &Policy{Path: "^/api/v[0-9]+/users/?$", PathType: "regex"}
	assert.True(t, matchPath(p3, "/api/v1/users"))
	assert.True(t, matchPath(p3, "/api/v99/users"))
	assert.False(t, matchPath(p3, "/api/users"))

	p4 := &Policy{Path: "", PathType: "exact"}
	assert.True(t, matchPath(p4, "/anything"))

	p5 := &Policy{Path: "*", PathType: "exact"}
	assert.True(t, matchPath(p5, "/anything"))
}

func TestEvaluateExactMatch(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{
		{
			ID: "allow-1", Name: "allow-user-api",
			Source: "spiffe://example.org/ns/svc/client",
			Destination: "spiffe://example.org/ns/svc/user-api",
			Methods: []string{"GET", "POST"},
			Path: "/api/v1/users", PathType: "exact",
			Effect: EffectAllow, Priority: 100, Enabled: true,
		},
	})

	// exact match
	d := e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "GET", Path: "/api/v1/users",
	})
	assert.True(t, d.Allowed)
	assert.Equal(t, "allow-1", d.PolicyID)

	// wrong method
	d = e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "DELETE", Path: "/api/v1/users",
	})
	assert.False(t, d.Allowed)

	// wrong source
	d = e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/other",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "GET", Path: "/api/v1/users",
	})
	assert.False(t, d.Allowed)
}

func TestEvaluatePriority(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{
		{
			ID: "deny-delete", Name: "deny delete",
			Source: "*", Destination: "spiffe://example.org/ns/svc/user-api",
			Methods: []string{"DELETE"}, Path: "*", PathType: "exact",
			Effect: EffectDeny, Priority: 200, Enabled: true,
		},
		{
			ID: "allow-all", Name: "allow all",
			Source: "spiffe://example.org/ns/svc/*",
			Destination: "spiffe://example.org/ns/svc/*",
			Methods: []string{"*"}, Path: "*", PathType: "exact",
			Effect: EffectAllow, Priority: 100, Enabled: true,
		},
	})

	// deny has higher priority, so DELETE is denied
	d := e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "DELETE", Path: "/api/v1/users",
	})
	assert.False(t, d.Allowed)
	assert.Equal(t, "deny-delete", d.PolicyID)

	// GET is allowed by the lower priority allow-all policy
	d = e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "GET", Path: "/api/v1/users",
	})
	assert.True(t, d.Allowed)
	assert.Equal(t, "allow-all", d.PolicyID)
}

func TestEvaluateDisabledPolicy(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{
		{
			ID: "disabled", Name: "disabled",
			Source: "*", Destination: "*",
			Methods: []string{"*"}, Path: "*", PathType: "exact",
			Effect: EffectAllow, Priority: 100, Enabled: false,
		},
	})

	d := e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/a",
		Destination: "spiffe://example.org/ns/svc/b",
		Method: "GET", Path: "/any",
	})
	assert.False(t, d.Allowed)
	assert.Equal(t, "no matching policy; default deny", d.Reason)
}

func TestEvaluateDefaultDeny(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{})

	d := e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/a",
		Destination: "spiffe://example.org/ns/svc/b",
		Method: "GET", Path: "/any",
	})
	assert.False(t, d.Allowed)
	assert.Equal(t, "no matching policy; default deny", d.Reason)
}

func TestEvaluateWildcard(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{
		{
			ID: "wildcard", Name: "wildcard",
			Source: "spiffe://example.org/ns/*",
			Destination: "spiffe://example.org/ns/*",
			Methods: []string{"GET"}, Path: "/api/*", PathType: "prefix",
			Effect: EffectAllow, Priority: 100, Enabled: true,
		},
	})

	d := e.Evaluate(Request{
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Method: "GET", Path: "/api/v1/users/123",
	})
	assert.True(t, d.Allowed)
}

func TestValidate(t *testing.T) {
	valid := &Policy{
		ID: "test", Source: "spiffe://a", Destination: "spiffe://b",
		Methods: []string{"GET"}, Effect: EffectAllow,
	}
	assert.NoError(t, Validate(valid))

	invalid := &Policy{
		ID: "", Source: "spiffe://a", Destination: "spiffe://b",
		Methods: []string{"GET"}, Effect: EffectAllow,
	}
	assert.Error(t, Validate(invalid))

	invalidEffect := &Policy{
		ID: "test", Source: "spiffe://a", Destination: "spiffe://b",
		Methods: []string{"GET"}, Effect: "invalid",
	}
	assert.Error(t, Validate(invalidEffect))

	invalidMethod := &Policy{
		ID: "test", Source: "spiffe://a", Destination: "spiffe://b",
		Methods: []string{"INVALID"}, Effect: EffectAllow,
	}
	assert.Error(t, Validate(invalidMethod))
}

func TestEngineSetAndAll(t *testing.T) {
	e := NewEngine()
	e.Set([]*Policy{
		{ID: "a", Name: "A", Source: "*", Destination: "*", Methods: []string{"*"}, Path: "*", PathType: "exact", Effect: EffectAllow, Priority: 100, Enabled: true},
		{ID: "b", Name: "B", Source: "*", Destination: "*", Methods: []string{"*"}, Path: "*", PathType: "exact", Effect: EffectAllow, Priority: 200, Enabled: true},
	})

	all := e.All()
	assert.Len(t, all, 2)
	assert.Equal(t, "b", all[0].ID)
	assert.Equal(t, "a", all[1].ID)

	p, ok := e.Get("a")
	assert.True(t, ok)
	assert.Equal(t, "A", p.Name)

	_, ok = e.Get("nonexistent")
	assert.False(t, ok)
}
