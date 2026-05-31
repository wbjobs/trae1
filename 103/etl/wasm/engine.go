package wasm

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"

	"github.com.bytecodealliance/wasmtime-go"
	"go.mongodb.org/mongo-driver/bson"
)

type Engine struct {
	store     *wasmtime.Store
	linker    *wasmtime.Linker
	module    *wasmtime.Module
	instance  *wasmtime.Instance
	mu        sync.RWMutex
	wasmBytes []byte
}

func NewEngine(wasmBytes []byte) (*Engine, error) {
	engine := wasmtime.NewEngine()
	store := wasmtime.NewStore(engine)

	linker, err := wasmtime.NewLinker(engine)
	if err != nil {
		return nil, fmt.Errorf("failed to create linker: %w", err)
	}

	module, err := wasmtime.NewModule(engine, wasmBytes)
	if err != nil {
		return nil, fmt.Errorf("failed to compile module: %w", err)
	}

	instance, err := linker.Instantiate(store, module)
	if err != nil {
		return nil, fmt.Errorf("failed to instantiate module: %w", err)
	}

	return &Engine{
		store:     store,
		linker:    linker,
		module:    module,
		instance:  instance,
		wasmBytes: wasmBytes,
	}, nil
}

func (e *Engine) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	docJSON, err := json.Marshal(doc)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal document: %w", err)
	}

	transform := e.instance.GetExport(e.store, "transform")
	if transform == nil {
		return nil, fmt.Errorf("transform function not found in wasm")
	}

	transformFunc := transform.Func()
	if transformFunc == nil {
		return nil, fmt.Errorf("transform is not a function")
	}

	memory := e.instance.GetExport(e.store, "memory")
	if memory == nil {
		return nil, fmt.Errorf("memory not found in wasm")
	}

	mem := memory.Memory()
	if mem == nil {
		return nil, fmt.Errorf("memory is not a memory type")
	}

	inputPtr, err := e.allocateString(e.store, mem, string(docJSON))
	if err != nil {
		return nil, fmt.Errorf("failed to allocate input: %w", err)
	}

	resultPtr, err := transformFunc.Call(e.store, inputPtr)
	if err != nil {
		return nil, fmt.Errorf("wasm execution error: %w", err)
	}

	resultAddr := resultPtr.(int64)
	resultStr, err := e.readString(e.store, mem, int(resultAddr))
	if err != nil {
		return nil, fmt.Errorf("failed to read result: %w", err)
	}

	var result map[string]interface{}
	if err := json.Unmarshal([]byte(resultStr), &result); err != nil {
		return nil, fmt.Errorf("failed to unmarshal result: %w", err)
	}

	resultBson := make(bson.M)
	for k, v := range result {
		resultBson[k] = v
	}

	return resultBson, nil
}

func (e *Engine) allocateString(store *wasmtime.Store, mem *wasmtime.Memory, s string) (int64, error) {
	data := []byte(s)
	size := len(data)

	alloc := e.instance.GetExport(store, "allocate")
	if alloc == nil {
		return 0, fmt.Errorf("allocate function not found")
	}

	allocFunc := alloc.Func()
	if allocFunc == nil {
		return 0, fmt.Errorf("allocate is not a function")
	}

	ptr, err := allocFunc.Call(store, size)
	if err != nil {
		return 0, err
	}

	ptrAddr := ptr.(int64)
	mem.Write(store, int(ptrAddr), data)

	return ptrAddr, nil
}

func (e *Engine) readString(store *wasmtime.Store, mem *wasmtime.Memory, ptr int) (string, error) {
	data := mem.Read(store, ptr, ptr+1000)
	for i := ptr; ; i++ {
		b := mem.Read(store, i, i+1)
		if b[0] == 0 {
			return string(mem.Read(store, ptr, i)), nil
		}
		if i-ptr > 10000 {
			return "", fmt.Errorf("string too long")
		}
	}
}

func (e *Engine) Reload(wasmBytes []byte) error {
	e.mu.Lock()
	defer e.mu.Unlock()

	engine := wasmtime.NewEngine()
	store := wasmtime.NewStore(engine)

	linker, err := wasmtime.NewLinker(engine)
	if err != nil {
		return fmt.Errorf("failed to create linker: %w", err)
	}

	module, err := wasmtime.NewModule(engine, wasmBytes)
	if err != nil {
		return fmt.Errorf("failed to compile module: %w", err)
	}

	instance, err := linker.Instantiate(store, module)
	if err != nil {
		return fmt.Errorf("failed to instantiate module: %w", err)
	}

	e.store = store
	e.linker = linker
	e.module = module
	e.instance = instance
	e.wasmBytes = wasmBytes

	return nil
}

func (e *Engine) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.instance = nil
	e.module = nil
	e.linker = nil
	e.store = nil
	return nil
}

type WasmStage struct {
	engine *Engine
}

func NewWasmStage(wasmBytes []byte) (*WasmStage, error) {
	engine, err := NewEngine(wasmBytes)
	if err != nil {
		return nil, err
	}
	return &WasmStage{engine: engine}, nil
}

func (s *WasmStage) Execute(ctx context.Context, doc bson.M) (bson.M, error) {
	return s.engine.Execute(ctx, doc)
}

func (s *WasmStage) Name() string {
	return "wasm_transform"
}
