package javascript

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"github.com/robertkrimen/otto"
	"go.mongodb.org/mongo-driver/bson"
)

type ScriptEngine struct {
	vm          *otto.Otto
	script      string
	mu          sync.RWMutex
	compiled    *otto.Script
	lastCompile time.Time
}

func NewScriptEngine(script string) (*ScriptEngine, error) {
	vm := otto.New()

	_, err := vm.Run(script)
	if err != nil {
		return nil, fmt.Errorf("failed to compile script: %w", err)
	}

	return &ScriptEngine{
		vm:       vm,
		script:   script,
		compiled: vm.MustCompile("pipeline.js", script),
	}, nil
}

func (e *ScriptEngine) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	docJSON, err := json.Marshal(doc)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal document: %w", err)
	}

	var result map[string]interface{}
	_, err = e.vm.Call("transform", nil, string(docJSON))
	if err != nil {
		return nil, fmt.Errorf("script execution error: %w", err)
	}

	resultBytes, err := json.Marshal(result)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal result: %w", err)
	}

	var resultDoc bson.M
	if err := json.Unmarshal(resultBytes, &resultDoc); err != nil {
		return nil, fmt.Errorf("failed to unmarshal result: %w", err)
	}

	return resultDoc, nil
}

func (e *ScriptEngine) Filter(ctx context.Context, doc bson.M) (bool, error) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	docJSON, err := json.Marshal(doc)
	if err != nil {
		return false, fmt.Errorf("failed to marshal document: %w", err)
	}

	result, err := e.vm.Call("filter", nil, string(docJSON))
	if err != nil {
		return false, fmt.Errorf("filter execution error: %w", err)
	}

	return result.ToBoolean()
}

func (e *ScriptEngine) Project(ctx context.Context, doc bson.M, fields []string) (bson.M, error) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	docJSON, err := json.Marshal(doc)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal document: %w", err)
	}

	result, err := e.vm.Call("project", nil, string(docJSON), fields)
	if err != nil {
		return nil, fmt.Errorf("project execution error: %w", err)
	}

	resultBytes, err := json.Marshal(result)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal result: %w", err)
	}

	var resultDoc bson.M
	if err := json.Unmarshal(resultBytes, &resultDoc); err != nil {
		return nil, fmt.Errorf("failed to unmarshal result: %w", err)
	}

	return resultDoc, nil
}

func (e *ScriptEngine) Reload(script string) error {
	e.mu.Lock()
	defer e.mu.Unlock()

	newVM := otto.New()
	_, err := newVM.Run(script)
	if err != nil {
		return fmt.Errorf("failed to compile new script: %w", err)
	}

	e.vm = newVM
	e.script = script
	e.compiled = e.vm.MustCompile("pipeline.js", script)
	e.lastCompile = time.Now()

	return nil
}

func (e *ScriptEngine) GetScript() string {
	e.mu.RLock()
	defer e.mu.RUnlock()
	return e.script
}

func (e *ScriptEngine) Close() error {
	e.vm.Interrupt()
	return nil
}

type JSFilterStage struct {
	engine *ScriptEngine
}

func NewJSFilterStage(script string) (*JSFilterStage, error) {
	engine, err := NewScriptEngine(fmt.Sprintf(`
		function filter(doc) {
			%s
			return %s;
		}
		function transform(doc) {
			return doc;
		}
		function project(doc, fields) {
			return doc;
		}
	`, script, script))
	if err != nil {
		return nil, err
	}
	return &JSFilterStage{engine: engine}, nil
}

func (s *JSFilterStage) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	pass, err := s.engine.Filter(ctx, doc)
	if err != nil {
		return nil, err
	}
	if !pass {
		return nil, nil
	}
	return doc, nil
}

func (s *JSFilterStage) Name() string {
	return "js_filter"
}

type JSProjectStage struct {
	engine  *ScriptEngine
	fields  []string
	renames map[string]string
}

func NewJSProjectStage(script string, renames map[string]string) (*JSProjectStage, error) {
	engine, err := NewScriptEngine(fmt.Sprintf(`
		function filter(doc) {
			return true;
		}
		function transform(doc) {
			return doc;
		}
		function project(doc, fields) {
			%s
			return doc;
		}
	`, script))
	if err != nil {
		return nil, err
	}
	return &JSProjectStage{
		engine:  engine,
		renames: renames,
	}, nil
}

func (s *JSProjectStage) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	return s.engine.Project(ctx, doc, s.fields)
}

func (s *JSProjectStage) Name() string {
	return "js_project"
}

type JSConvertStage struct {
	engine    *ScriptEngine
	fieldType map[string]string
}

func NewJSConvertStage(script string, fieldType map[string]string) (*JSConvertStage, error) {
	engine, err := NewScriptEngine(fmt.Sprintf(`
		function filter(doc) {
			return true;
		}
		function transform(doc) {
			%s
			return doc;
		}
		function project(doc, fields) {
			return doc;
		}
	`, script))
	if err != nil {
		return nil, err
	}
	return &JSConvertStage{engine: engine, fieldType: fieldType}, nil
}

func (s *JSConvertStage) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	return s.engine.Execute(ctx, doc)
}

func (s *JSConvertStage) Name() string {
	return "js_convert"
}

type JSTransformStage struct {
	engine *ScriptEngine
}

func NewJSTransformStage(script string) (*JSTransformStage, error) {
	engine, err := NewScriptEngine(fmt.Sprintf(`
		function filter(doc) {
			return true;
		}
		function transform(doc) {
			%s
			return doc;
		}
		function project(doc, fields) {
			return doc;
		}
	`, script))
	if err != nil {
		return nil, err
	}
	return &JSTransformStage{engine: engine}, nil
}

func (s *JSTransformStage) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	return s.engine.Execute(ctx, doc)
}

func (s *JSTransformStage) Name() string {
	return "js_transform"
}
