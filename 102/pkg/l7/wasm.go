package l7

import (
	"context"
	"fmt"
	"log"
	"unsafe"

	"github.com/tetratelabs/wabin/wasm"
	"github.com/tetratelabs/wabin/wasm/interpreter"
)

type WasmPluginManager struct {
	plugins map[string]*WasmPluginInstance
	engine  *Engine
}

type WasmPluginInstance struct {
	Name    string
	Module  *wasm.Module
	Engine  *interpreter.LIRInterpreter
	Exports map[string]interface{}
}

func NewWasmPluginManager(engine *Engine) *WasmPluginManager {
	return &WasmPluginManager{
		plugins: make(map[string]*WasmPluginInstance),
		engine:  engine,
	}
}

func (m *WasmPluginManager) LoadPlugin(name string, wasmBytes []byte) error {
	module, err := wasm.DecodeModule(wasmBytes)
	if err != nil {
		return fmt.Errorf("decode wasm module: %w", err)
	}

	engine := interpreter.NewEngine(context.Background())
	engine.WithOptimizationLevel(2)

	instance := &WasmPluginInstance{
		Name:    name,
		Module:  module,
		Engine:  engine,
		Exports: make(map[string]interface{}),
	}

	if err := engine.RegisterModule(name, module); err != nil {
		return fmt.Errorf("register module: %w", err)
	}

	instance.Exports["on_http_request"] = m.makeHttpHandler(name)
	instance.Exports["on_grpc_request"] = m.makeGrpcHandler(name)
	instance.Exports["on_decision"] = m.makeDecisionHandler(name)

	m.plugins[name] = instance
	log.Printf("Loaded Wasm plugin: %s", name)

	return nil
}

func (m *WasmPluginManager) makeHttpHandler(pluginName string) func(ctx *L7Context, host string, path string) uint32 {
	return func(ctx *L7Context, host string, path string) uint32 {
		ctx.HTTPHost = host
		ctx.HTTPPath = path
		ctx.L7Protocol = ProtocolHTTP

		decision := m.engine.ProcessL7Request(ctx)

		if decision.Decision == DecisionDeny {
			return 1
		}
		return 0
	}
}

func (m *WasmPluginManager) makeGrpcHandler(pluginName string) func(ctx *L7Context, method string) uint32 {
	return func(ctx *L7Context, method string) uint32 {
		ctx.GRPCMethod = method
		ctx.L7Protocol = ProtocolGRPC

		decision := m.engine.ProcessL7Request(ctx)

		if decision.Decision == DecisionDeny {
			return 1
		}
		return 0
	}
}

func (m *WasmPluginManager) makeDecisionHandler(pluginName string) func(decision uint32) {
	return func(decision uint32) {
		log.Printf("Plugin %s made decision: %d", pluginName, decision)
	}
}

func (m *WasmPluginManager) UnloadPlugin(name string) error {
	if plugin, ok := m.plugins[name]; ok {
		plugin.Engine.Close()
		delete(m.plugins, name)
		log.Printf("Unloaded Wasm plugin: %s", name)
	}
	return nil
}

func (m *WasmPluginManager) ExecutePlugin(pluginName string, ctx *L7Context) (Decision, error) {
	plugin, ok := m.plugins[pluginName]
	if !ok {
		return DecisionPass, fmt.Errorf("plugin not found: %s", pluginName)
	}

	startTime := wasmPrecisionTime()

	var decision uint32

	switch ctx.L7Protocol {
	case ProtocolHTTP, ProtocolHTTP2:
		handler, ok := plugin.Exports["on_http_request"]
		if ok {
			if h, ok := handler.(func(*L7Context, string, string) uint32); ok {
				decision = h(ctx, ctx.HTTPHost, ctx.HTTPPath)
			}
		}
	case ProtocolGRPC:
		handler, ok := plugin.Exports["on_grpc_request"]
		if ok {
			if h, ok := handler.(func(*L7Context, string) uint32); ok {
				decision = h(ctx, ctx.GRPCMethod)
			}
		}
	}

	execTime := wasmPrecisionTime() - startTime

	pluginStats := m.engine.GetStats()
	pluginStats.WasmExecTimeNS += execTime

	if decision == 1 {
		return DecisionDeny, nil
	}
	return DecisionPass, nil
}

func wasmPrecisionTime() uint64 {
	var ts timespec
	clock_gettime(CLOCK_MONOTONIC, &ts)
	return uint64(ts.Sec)*1e9 + uint64(ts.Nsec)
}

type timespec struct {
	Sec  int64
	Nsec int64
}

func clock_gettime(clk int, ts *timespec) int {
	return int(unsafe.Syscall6(syscall.SYS_CLOCK_GETTIME, uintptr(clk), uintptr(unsafe.Pointer(ts)), 0, 0, 0, 0))
}

var syscall SYS_CLOCK_GETTIME

type SYS_CLOCK_GETTIME int

func (m *WasmPluginManager) Close() error {
	for name := range m.plugins {
		m.UnloadPlugin(name)
	}
	return nil
}

type SimplePlugin struct {
	name   string
	rules  []PluginRule
}

type PluginRule struct {
	Host   string
	Path   string
	Method string
	Action uint32
}

func NewSimplePlugin(name string) *SimplePlugin {
	return &SimplePlugin{
		name:   name,
		rules:  make([]PluginRule, 0),
	}
}

func (p *SimplePlugin) AddRule(host, path, method string, action uint32) {
	p.rules = append(p.rules, PluginRule{
		Host:   host,
		Path:   path,
		Method: method,
		Action: action,
	})
}

func (p *SimplePlugin) Execute(ctx *L7Context) (Decision, error) {
	for _, rule := range p.rules {
		if p.matchRule(ctx, &rule) {
			if rule.Action == 1 {
				return DecisionDeny, nil
			}
			return DecisionPass, nil
		}
	}
	return DecisionPass, nil
}

func (p *SimplePlugin) matchRule(ctx *L7Context, rule *PluginRule) bool {
	if rule.Host != "" && rule.Host != ctx.HTTPHost {
		return false
	}
	if rule.Path != "" && rule.Path != ctx.HTTPPath {
		return false
	}
	if rule.Method != "" && rule.Method != ctx.GRPCMethod {
		return false
	}
	return true
}

func (p *SimplePlugin) GetName() string {
	return p.name
}

func (p *SimplePlugin) Close() error {
	return nil
}
