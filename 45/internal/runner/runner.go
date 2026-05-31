package runner

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/grpctest/grpctest/internal/assert"
	"github.com/grpctest/grpctest/internal/config"
	"github.com/grpctest/grpctest/internal/invoke"
	"github.com/grpctest/grpctest/internal/report"
	"github.com/grpctest/grpctest/internal/schema"
	"github.com/grpctest/grpctest/internal/vars"
)

type Runner struct {
	cfg    *config.Config
	store  *vars.Store
	provider *schema.Provider
	caller *invoke.Caller
}

func NewRunner(cfg *config.Config) *Runner {
	return &Runner{
		cfg:   cfg,
		store: vars.NewStore(),
	}
}

func (r *Runner) Run(ctx context.Context) ([]report.SuiteResult, error) {
	var err error
	r.provider, err = schema.NewProvider(ctx, schema.Options{
		ProtoFiles:    r.cfg.Target.ProtoFiles,
		ImportPaths:   r.cfg.Target.ImportPaths,
		UseReflection: r.cfg.Target.UseReflection,
		Address:       r.cfg.Target.Address,
		Insecure:      r.cfg.Target.Insecure,
		Timeout:       r.cfg.Target.Timeout,
	})
	if err != nil {
		return nil, fmt.Errorf("init schema provider: %w", err)
	}
	defer r.provider.Close()

	r.caller, err = invoke.NewCaller(ctx, r.cfg.Target)
	if err != nil {
		return nil, fmt.Errorf("init caller: %w", err)
	}
	defer r.caller.Close()

	for k, v := range r.cfg.Target.Headers {
		r.store.Set(k, v)
	}

	suite := report.SuiteResult{Name: "grpc-contract"}
	for _, s := range r.cfg.Steps {
		c := r.runStep(ctx, s)
		suite.Cases = append(suite.Cases, c)
		if c.Err != nil || !allPassed(c) {
			r.store.Set(s.ID+"._failed", true)
		}
	}
	return []report.SuiteResult{suite}, nil
}

func allPassed(c report.CaseResult) bool {
	for _, a := range c.Assertions {
		if !a.Passed {
			return false
		}
	}
	return true
}

func (r *Runner) runStep(ctx context.Context, step config.Step) report.CaseResult {
	c := report.CaseResult{
		Name:      step.ID,
		Classname: step.Service + "." + step.Method,
	}
	for _, dep := range step.DependsOn {
		if failed, _ := r.store.Get(dep + "._failed"); failed != nil {
			c.Err = fmt.Errorf("dependency step %s failed, skipping", dep)
			return c
		}
		if _, ok := r.store.Get(dep); !ok {
			c.Err = fmt.Errorf("dependency step %s not found in store", dep)
			return c
		}
	}
	md, err := r.provider.FindMethod(step.Service, step.Method)
	if err != nil {
		c.Err = err
		return c
	}
	result := r.caller.Invoke(ctx, md, step, r.store)
	c.Duration = result.Duration
	if result.Err != nil {
		c.Err = result.Err
		c.Details = fmt.Sprintf("call failed: %v", result.Err)
		return c
	}
	respJSON, _ := json.Marshal(result.Response)
	c.Details = string(respJSON)

	r.store.Set(step.ID, result.Response)
	r.store.Set(step.ID+"._response", result.Response)
	r.store.Set(step.ID+"._duration", result.Duration.Milliseconds())

	as := make([]assert.Assertion, 0, len(step.Assertions))
	for _, a := range step.Assertions {
		as = append(as, assert.Assertion(a))
	}
	ar := assert.Run(r.store, result.Response, as)
	status := func(pass bool) string {
		if pass {
			return "PASS"
		}
		return "FAIL"
	}
	for _, r2 := range ar {
		detail := fmt.Sprintf("[%s] %s %s: actual=%v expected=%v",
			status(r2.Passed),
			r2.Field, r2.Operator, r2.Actual, r2.Expected)
		if !r2.Passed {
			detail += " (" + r2.Error + ")"
		}
		c.Assertions = append(c.Assertions, report.AssertionResult{
			Passed: r2.Passed,
			Detail: detail,
		})
	}
	return c
}
