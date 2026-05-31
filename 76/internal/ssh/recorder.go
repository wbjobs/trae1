package ssh

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"sync"
	"time"

	"bastion/internal/models"
	"bastion/internal/terminal"
)

type AsciinemaHeader struct {
	Version   int               `json:"version"`
	Width     int               `json:"width"`
	Height    int               `json:"height"`
	Timestamp int64             `json:"timestamp"`
	Title     string            `json:"title"`
	Env       map[string]string `json:"env"`
}

type AsciinemaEvent struct {
	Time float64     `json:"time"`
	Type string      `json:"type"`
	Data interface{} `json:"data"`
}

type ResizeData struct {
	Width  int `json:"width"`
	Height int `json:"height"`
}

type FrameStore struct {
	frames []terminal.Frame
	mu     sync.RWMutex
}

func NewFrameStore() *FrameStore {
	return &FrameStore{
		frames: make([]terminal.Frame, 0),
	}
}

func (fs *FrameStore) Add(f terminal.Frame) {
	fs.mu.Lock()
	defer fs.mu.Unlock()
	fs.frames = append(fs.frames, f)
}

func (fs *FrameStore) GetAll() []terminal.Frame {
	fs.mu.RLock()
	defer fs.mu.RUnlock()
	result := make([]terminal.Frame, len(fs.frames))
	copy(result, fs.frames)
	return result
}

func (fs *FrameStore) GetAt(ts float64) *terminal.Frame {
	fs.mu.RLock()
	defer fs.mu.RUnlock()
	return terminal.FrameFromTimestamp(fs.frames, ts)
}

func (fs *FrameStore) GetRange(from, to float64) []terminal.Frame {
	fs.mu.RLock()
	defer fs.mu.RUnlock()
	var result []terminal.Frame
	for _, f := range fs.frames {
		if f.Timestamp >= from && f.Timestamp <= to {
			result = append(result, f)
		}
	}
	return result
}

func (fs *FrameStore) Search(query string, fromTs float64, maxResults int) []terminal.SearchResult {
	fs.mu.RLock()
	defer fs.mu.RUnlock()
	return terminal.SearchFrames(fs.frames, query, fromTs, maxResults)
}

func (fs *FrameStore) Len() int {
	fs.mu.RLock()
	defer fs.mu.RUnlock()
	return len(fs.frames)
}

type Recorder struct {
	file          *os.File
	encoder       *json.Encoder
	startTime     time.Time
	session       *models.Session
	mu            sync.Mutex
	closed        bool
	header        AsciinemaHeader
	term          *terminal.Terminal
	frameStore    *FrameStore
	frameInterval time.Duration
	lastFrame     time.Time
	frameTicker   *time.Ticker
	frameDone     chan struct{}
}

func NewRecorder(filePath string, session *models.Session, width, height int) (*Recorder, error) {
	f, err := os.Create(filePath)
	if err != nil {
		return nil, fmt.Errorf("create recording file: %w", err)
	}

	header := AsciinemaHeader{
		Version:   2,
		Width:     width,
		Height:    height,
		Timestamp: time.Now().Unix(),
		Title:     fmt.Sprintf("Session %s - %s@%s", session.ID, session.TargetUser, session.TargetHost),
		Env: map[string]string{
			"SHELL": "/bin/bash",
			"TERM":  "xterm-256color",
		},
	}

	encoder := json.NewEncoder(f)
	encoder.SetEscapeHTML(false)

	if err := encoder.Encode(header); err != nil {
		f.Close()
		return nil, fmt.Errorf("write asciinema header: %w", err)
	}

	frameStore := NewFrameStore()

	return &Recorder{
		file:          f,
		encoder:       encoder,
		startTime:     time.Now(),
		session:       session,
		header:        header,
		term:          terminal.NewTerminal(width, height),
		frameStore:    frameStore,
		frameInterval: 100 * time.Millisecond,
		lastFrame:     time.Now(),
		frameDone:     make(chan struct{}),
	}, nil
}

func (r *Recorder) StartFrameCapture() {
	r.frameTicker = time.NewTicker(r.frameInterval)
	go func() {
		for {
			select {
			case <-r.frameTicker.C:
				r.captureFrame()
			case <-r.frameDone:
				return
			}
		}
	}()
}

func (r *Recorder) captureFrame() {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed {
		return
	}
	elapsed := time.Since(r.startTime).Seconds()
	frame := r.term.Snapshot()
	frame.Timestamp = elapsed
	r.frameStore.Add(frame)
}

func (r *Recorder) WriteInput(data []byte) {
	r.writeEvent("i", data)
}

func (r *Recorder) WriteOutput(data []byte) {
	r.writeEvent("o", data)
	r.term.Write(data)
	r.broadcast(data)

	r.mu.Lock()
	if time.Since(r.lastFrame) >= r.frameInterval {
		r.lastFrame = time.Now()
		elapsed := time.Since(r.startTime).Seconds()
		frame := r.term.Snapshot()
		frame.Timestamp = elapsed
		r.frameStore.Add(frame)
	}
	r.mu.Unlock()
}

func (r *Recorder) WriteResize(width, height int) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed {
		return
	}

	elapsed := time.Since(r.startTime).Seconds()
	event := AsciinemaEvent{
		Time: elapsed,
		Type: "r",
		Data: ResizeData{Width: width, Height: height},
	}

	if err := r.encoder.Encode(event); err != nil {
		fmt.Fprintf(os.Stderr, "write resize event error: %v\n", err)
	}

	r.term.Resize(width, height)
	r.header.Width = width
	r.header.Height = height
}

func (r *Recorder) writeEvent(eventType string, data []byte) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed {
		return
	}

	elapsed := time.Since(r.startTime).Seconds()
	event := AsciinemaEvent{
		Time: elapsed,
		Type: eventType,
		Data: string(data),
	}

	if err := r.encoder.Encode(event); err != nil {
		fmt.Fprintf(os.Stderr, "write event error: %v\n", err)
	}
}

func (r *Recorder) broadcast(data []byte) {
	if r.session.LiveStream != nil {
		r.session.LiveStream.Broadcast(data)
	}
}

func (r *Recorder) Close() error {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closed {
		return nil
	}
	r.closed = true

	elapsed := time.Since(r.startTime).Seconds()
	frame := r.term.Snapshot()
	frame.Timestamp = elapsed
	r.frameStore.Add(frame)

	if r.frameTicker != nil {
		r.frameTicker.Stop()
		close(r.frameDone)
	}

	return r.file.Close()
}

func (r *Recorder) FilePath() string {
	if r.file != nil {
		return r.file.Name()
	}
	return ""
}

func (r *Recorder) FrameStore() *FrameStore {
	return r.frameStore
}

func (r *Recorder) RenderedTextAt(ts float64) string {
	frame := r.frameStore.GetAt(ts)
	if frame == nil {
		return ""
	}
	return frame.RenderText()
}

func (r *Recorder) SearchFrames(query string, fromTs float64, maxResults int) []terminal.SearchResult {
	return r.frameStore.Search(query, fromTs, maxResults)
}

type TeeWriter struct {
	writers []io.Writer
}

func NewTeeWriter(writers ...io.Writer) *TeeWriter {
	return &TeeWriter{writers: writers}
}

func (tw *TeeWriter) Write(p []byte) (int, error) {
	for _, w := range tw.writers {
		if _, err := w.Write(p); err != nil {
			return 0, err
		}
	}
	return len(p), nil
}
