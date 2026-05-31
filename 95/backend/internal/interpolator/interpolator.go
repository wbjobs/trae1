package interpolator

import (
	"sync"
	"time"

	"cloud-gamepad/internal/protocol"
)

type Frame struct {
	Seq       uint64
	Timestamp uint64
	Buttons   [protocol.MaxButtons]bool
	Axes      [protocol.MaxAxes]float32
}

type Interpolator struct {
	mu    sync.RWMutex
	ring  [4]Frame
	head  int
	size  int
	lastSeq uint64
}

func New() *Interpolator {
	return &Interpolator{}
}

func (it *Interpolator) Push(p *protocol.InputPacket) {
	it.mu.Lock()
	defer it.mu.Unlock()
	f := Frame{
		Seq:       p.Seq,
		Timestamp: p.Timestamp,
		Buttons:   p.Buttons,
		Axes:      p.Axes,
	}
	it.ring[it.head] = f
	it.head = (it.head + 1) % len(it.ring)
	if it.size < len(it.ring) {
		it.size++
	}
	if p.Seq > it.lastSeq {
		it.lastSeq = p.Seq
	}
}

func (it *Interpolator) Estimate() Frame {
	it.mu.RLock()
	defer it.mu.RUnlock()
	if it.size == 0 {
		return Frame{}
	}
	prev := it.ring[(it.head-1+len(it.ring))%len(it.ring)]
	if it.size < 2 {
		return prev
	}
	pp := it.ring[(it.head-2+len(it.ring))%len(it.ring)]

	out := Frame{
		Seq:       prev.Seq + 1,
		Timestamp: uint64(time.Now().UnixMilli()),
	}
	for i := 0; i < protocol.MaxButtons; i++ {
		out.Buttons[i] = prev.Buttons[i]
	}
	for i := 0; i < protocol.MaxAxes; i++ {
		a := pp.Axes[i]
		b := prev.Axes[i]
		out.Axes[i] = a + (b-a)*0.5
	}
	_ = pp
	return out
}

func (it *Interpolator) LastSeq() uint64 {
	it.mu.RLock()
	defer it.mu.RUnlock()
	return it.lastSeq
}
