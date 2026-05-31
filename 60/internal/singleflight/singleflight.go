package singleflight

import (
	"sync"
	"sync/atomic"
)

type call struct {
	wg        sync.WaitGroup
	val       interface{}
	err       error
	callCount atomic.Int64
}

type Result struct {
	Val          interface{}
	Err          error
	IsPrimary    bool
	WaitCount    int64
}

type Group struct {
	mu sync.Mutex
	m  map[string]*call
}

func New() *Group {
	return &Group{
		m: make(map[string]*call),
	}
}

func (g *Group) Do(key string, fn func() (interface{}, error)) (interface{}, error) {
	result := g.DoWithPosition(key, fn)
	return result.Val, result.Err
}

func (g *Group) DoWithPosition(key string, fn func() (interface{}, error)) Result {
	g.mu.Lock()
	if c, ok := g.m[key]; ok {
		c.callCount.Add(1)
		g.mu.Unlock()
		c.wg.Wait()
		return Result{
			Val:       c.val,
			Err:       c.err,
			IsPrimary: false,
			WaitCount: c.callCount.Load(),
		}
	}

	c := new(call)
	c.wg.Add(1)
	c.callCount.Store(0)
	g.m[key] = c
	g.mu.Unlock()

	c.val, c.err = fn()
	c.wg.Done()

	g.mu.Lock()
	delete(g.m, key)
	g.mu.Unlock()

	return Result{
		Val:       c.val,
		Err:       c.err,
		IsPrimary: true,
		WaitCount: c.callCount.Load(),
	}
}

func (g *Group) Forget(key string) {
	g.mu.Lock()
	delete(g.m, key)
	g.mu.Unlock()
}
