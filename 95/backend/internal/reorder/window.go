package reorder

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"cloud-gamepad/internal/protocol"
)

const (
	DefaultWindowSize = 64
	DefaultTimeoutMs  = 20
)

type EventType int

const (
	EventReordered EventType = iota
	EventTimeout
	EventGap
)

type Event struct {
	Type      EventType
	Expected  uint16
	Received  uint16
	Timestamp time.Time
	Message   string
}

type Handler func(p *protocol.InputPacket)

type Window struct {
	mu            sync.Mutex
	windowSize    int
	timeout       time.Duration
	buf           []*protocol.InputPacket
	hasPacket     []bool
	nextExpected  uint16
	nextDeliver   uint16
	handler       Handler
	eventCallback func(Event)
	timer         *time.Timer
	running       bool
	ctx           context.Context
	cancel        context.CancelFunc
	lastActive    time.Time
	highestSeq    uint16
}

func seqDiff(a, b uint16) int {
	return int(int16(a - b))
}

func seqGT(a, b uint16) bool { return seqDiff(a, b) > 0 }
func seqGE(a, b uint16) bool { return seqDiff(a, b) >= 0 }
func seqLT(a, b uint16) bool { return seqDiff(a, b) < 0 }
func seqLE(a, b uint16) bool { return seqDiff(a, b) <= 0 }

func New(windowSize int, timeoutMs int, handler Handler) *Window {
	if windowSize <= 0 {
		windowSize = DefaultWindowSize
	}
	if timeoutMs <= 0 {
		timeoutMs = DefaultTimeoutMs
	}
	if windowSize > 1024 {
		windowSize = 1024
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &Window{
		windowSize: windowSize,
		timeout:    time.Duration(timeoutMs) * time.Millisecond,
		buf:        make([]*protocol.InputPacket, windowSize),
		hasPacket:  make([]bool, windowSize),
		handler:    handler,
		ctx:        ctx,
		cancel:     cancel,
	}
}

func (w *Window) OnEvent(fn func(Event)) {
	w.eventCallback = fn
}

func (w *Window) Start() {
	w.mu.Lock()
	if w.running {
		w.mu.Unlock()
		return
	}
	w.running = true
	w.timer = time.AfterFunc(w.timeout, w.onTimeout)
	w.mu.Unlock()
}

func (w *Window) Stop() {
	w.mu.Lock()
	defer w.mu.Unlock()
	if !w.running {
		return
	}
	w.running = false
	w.cancel()
	if w.timer != nil {
		w.timer.Stop()
	}
}

func (w *Window) Push(p *protocol.InputPacket) {
	seq := uint16(p.Seq & 0xFFFF)

	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.running {
		return
	}

	now := time.Now()
	w.lastActive = now

	if w.highestSeq == 0 && seqGT(seq, 10000) {
		w.nextExpected = seq
		w.nextDeliver = seq
	}

	if seqLT(seq, w.nextDeliver) {
		return
	}

	if seqGE(seq, uint16(w.nextDeliver+uint16(w.windowSize))) {
		if w.eventCallback != nil {
			w.eventCallback(Event{
				Type:      EventGap,
				Expected:  w.nextDeliver,
				Received:  seq,
				Timestamp: now,
				Message:   fmt.Sprintf("packet %d outside window [%d, %d), forcing advance",
					seq, w.nextDeliver, w.nextDeliver+uint16(w.windowSize)),
			})
		}
		log.Printf("[reorder] gap: seq=%d outside window, advance from %d to %d",
			seq, w.nextDeliver, uint16(seq-uint16(w.windowSize/2)))
		w.advanceTo(uint16(seq - uint16(w.windowSize/2)))
	}

	idx := int(seq) % w.windowSize
	if w.hasPacket[idx] {
		return
	}

	w.buf[idx] = p
	w.hasPacket[idx] = true

	if seqGT(seq, w.highestSeq) {
		w.highestSeq = seq
	}

	expected := w.nextExpected
	if seq != expected && seqGT(seq, expected) {
		if w.eventCallback != nil {
			w.eventCallback(Event{
				Type:      EventReordered,
				Expected:  expected,
				Received:  seq,
				Timestamp: now,
				Message:   fmt.Sprintf("reorder: expected %d, got %d (gap=%d)",
					expected, seq, seqDiff(seq, expected)),
			})
		}
		log.Printf("[reorder] reorder: expected=%d, received=%d, diff=%d",
			expected, seq, seqDiff(seq, expected))
	}

	w.deliverContiguous()

	if w.timer != nil {
		w.timer.Reset(w.timeout)
	}
}

func (w *Window) deliverContiguous() {
	for {
		idx := int(w.nextDeliver) % w.windowSize
		if !w.hasPacket[idx] {
			break
		}
		p := w.buf[idx]
		w.hasPacket[idx] = false
		w.buf[idx] = nil
		if seqGE(w.nextDeliver, w.nextExpected) {
			w.nextExpected = w.nextDeliver + 1
		}
		w.nextDeliver++
		if w.handler != nil {
			w.handler(p)
		}
	}
}

func (w *Window) advanceTo(target uint16) {
	for seqLT(w.nextDeliver, target) {
		idx := int(w.nextDeliver) % w.windowSize
		if w.hasPacket[idx] {
			w.hasPacket[idx] = false
			w.buf[idx] = nil
		}
		w.nextDeliver++
	}
	w.nextExpected = w.nextDeliver
	w.deliverContiguous()
}

func (w *Window) onTimeout() {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.running {
		return
	}

	if w.nextDeliver == w.highestSeq {
		w.timer.Reset(w.timeout)
		return
	}

	now := time.Now()
	if now.Sub(w.lastActive) < w.timeout {
		w.timer.Reset(w.timeout)
		return
	}

	var best *protocol.InputPacket
	var bestSeq uint16
	for i := 0; i < w.windowSize; i++ {
		seq := uint16(int(w.nextDeliver) + i)
		idx := int(seq) % w.windowSize
		if w.hasPacket[idx] {
			best = w.buf[idx]
			bestSeq = seq
		} else {
			break
		}
	}

	if best != nil && seqGT(bestSeq, w.nextExpected-1) {
		if w.eventCallback != nil {
			w.eventCallback(Event{
				Type:      EventTimeout,
				Expected:  w.nextDeliver,
				Received:  bestSeq,
				Timestamp: now,
				Message:   fmt.Sprintf("timeout: skipped %d packets, using seq=%d",
					seqDiff(bestSeq, w.nextDeliver)+1, bestSeq),
			})
		}
		log.Printf("[reorder] timeout: skipping %d packets (%d..%d), using latest seq=%d",
			seqDiff(bestSeq, w.nextDeliver)+1, w.nextDeliver, bestSeq-1, bestSeq)
		w.advanceTo(bestSeq + 1)
	}

	w.timer.Reset(w.timeout)
}

func (w *Window) Stats() (nextDeliver uint16, highest uint16, inFlight int) {
	w.mu.Lock()
	defer w.mu.Unlock()
	cnt := 0
	for _, h := range w.hasPacket {
		if h {
			cnt++
		}
	}
	return w.nextDeliver, w.highestSeq, cnt
}
