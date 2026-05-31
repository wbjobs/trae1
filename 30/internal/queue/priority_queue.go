package queue

import (
	"container/list"
	"fmt"
	"sync"
	"time"

	"transcode-gateway/internal/model"
)

type Item struct {
	Task       *model.Task
	EnqueuedAt time.Time
	element    *list.Element
}

type PriorityQueue struct {
	high   *list.List
	medium *list.List
	low    *list.List

	running  map[string]*Item
	finished map[string]struct{}

	capacity      int
	highReserved  int
	mediumReserved int

	mu sync.Mutex

	arrivalCh chan struct{}
}

func New(capacity, highReserved, mediumReserved int) *PriorityQueue {
	if capacity <= 0 {
		capacity = 4
	}
	return &PriorityQueue{
		high:          list.New(),
		medium:        list.New(),
		low:           list.New(),
		running:       make(map[string]*Item),
		finished:      make(map[string]struct{}),
		capacity:      capacity,
		highReserved:  highReserved,
		mediumReserved: mediumReserved,
		arrivalCh:     make(chan struct{}, 1),
	}
}

func (q *PriorityQueue) Enqueue(t *model.Task) error {
	if t == nil {
		return fmt.Errorf("task is nil")
	}

	q.mu.Lock()
	defer q.mu.Unlock()

	if _, ok := q.running[t.ID]; ok {
		return fmt.Errorf("task %s already running", t.ID)
	}
	if _, ok := q.finished[t.ID]; ok {
		return fmt.Errorf("task %s already finished", t.ID)
	}

	item := &Item{Task: t, EnqueuedAt: time.Now()}

	switch t.Priority {
	case model.PriorityHigh:
		item.element = q.high.PushBack(item)
	case model.PriorityMedium:
		item.element = q.medium.PushBack(item)
	case model.PriorityLow:
		item.element = q.low.PushBack(item)
	default:
		item.element = q.medium.PushBack(item)
	}

	q.notify()
	return nil
}

func (q *PriorityQueue) Dequeue() (*Item, bool) {
	q.mu.Lock()
	defer q.mu.Unlock()

	highRunning := q.countRunningByPriority(model.PriorityHigh)
	mediumRunning := q.countRunningByPriority(model.PriorityMedium)

	highAvailable := q.highReserved - highRunning
	mediumAvailable := q.mediumReserved - mediumRunning
	generalAvailable := q.capacity - len(q.running)

	if q.high.Len() > 0 && (highAvailable > 0 || generalAvailable > 0) {
		e := q.high.Front()
		item := e.Value.(*Item)
		q.high.Remove(e)
		item.element = nil
		q.running[item.Task.ID] = item
		return item, true
	}

	if q.medium.Len() > 0 && (mediumAvailable > 0 || (generalAvailable > 0 && q.high.Len() == 0)) {
		e := q.medium.Front()
		item := e.Value.(*Item)
		q.medium.Remove(e)
		item.element = nil
		q.running[item.Task.ID] = item
		return item, true
	}

	if q.low.Len() > 0 && generalAvailable > 0 && q.high.Len() == 0 && q.medium.Len() == 0 {
		e := q.low.Front()
		item := e.Value.(*Item)
		q.low.Remove(e)
		item.element = nil
		q.running[item.Task.ID] = item
		return item, true
	}

	return nil, false
}

func (q *PriorityQueue) Complete(taskID string) {
	q.mu.Lock()
	defer q.mu.Unlock()

	if _, ok := q.running[taskID]; ok {
		delete(q.running, taskID)
		q.finished[taskID] = struct{}{}
		q.notify()
	}
}

func (q *PriorityQueue) RemoveFromQueue(taskID string) bool {
	q.mu.Lock()
	defer q.mu.Unlock()

	removeFromList := func(l *list.List) bool {
		for e := l.Front(); e != nil; e = e.Next() {
			item := e.Value.(*Item)
			if item.Task.ID == taskID {
				l.Remove(e)
				return true
			}
		}
		return false
	}

	if removeFromList(q.high) {
		return true
	}
	if removeFromList(q.medium) {
		return true
	}
	if removeFromList(q.low) {
		return true
	}
	return false
}

func (q *PriorityQueue) IsRunning(taskID string) bool {
	q.mu.Lock()
	defer q.mu.Unlock()
	_, ok := q.running[taskID]
	return ok
}

func (q *PriorityQueue) RunningCount() int {
	q.mu.Lock()
	defer q.mu.Unlock()
	return len(q.running)
}

func (q *PriorityQueue) Capacity() int {
	return q.capacity
}

func (q *PriorityQueue) countRunningByPriority(p model.Priority) int {
	count := 0
	for _, item := range q.running {
		if item.Task.Priority == p {
			count++
		}
	}
	return count
}

func (q *PriorityQueue) Snapshot() model.QueueSnapshot {
	q.mu.Lock()
	defer q.mu.Unlock()

	highIDs := make([]string, 0, q.high.Len())
	for e := q.high.Front(); e != nil; e = e.Next() {
		highIDs = append(highIDs, e.Value.(*Item).Task.ID)
	}

	mediumIDs := make([]string, 0, q.medium.Len())
	for e := q.medium.Front(); e != nil; e = e.Next() {
		mediumIDs = append(mediumIDs, e.Value.(*Item).Task.ID)
	}

	lowIDs := make([]string, 0, q.low.Len())
	for e := q.low.Front(); e != nil; e = e.Next() {
		lowIDs = append(lowIDs, e.Value.(*Item).Task.ID)
	}

	runningIDs := make([]string, 0, len(q.running))
	for id := range q.running {
		runningIDs = append(runningIDs, id)
	}

	return model.QueueSnapshot{
		HighPending:   highIDs,
		MediumPending: mediumIDs,
		LowPending:    lowIDs,
		Running:       runningIDs,
		Capacity:      q.capacity,
		RunningCount:  len(q.running),
	}
}

func (q *PriorityQueue) ArrivalCh() <-chan struct{} {
	return q.arrivalCh
}

func (q *PriorityQueue) notify() {
	select {
	case q.arrivalCh <- struct{}{}:
	default:
	}
}

func (q *PriorityQueue) PreemptIfNeeded() (*Item, bool) {
	q.mu.Lock()
	defer q.mu.Unlock()

	if q.high.Len() == 0 {
		return nil, false
	}

	highRunning := q.countRunningByPriority(model.PriorityHigh)
	if highRunning < q.highReserved {
		return nil, false
	}

	generalAvailable := q.capacity - len(q.running)
	if generalAvailable > 0 {
		return nil, false
	}

	for _, item := range q.running {
		if item.Task.Priority == model.PriorityLow {
			delete(q.running, item.Task.ID)
			item.Task.SetStatus(model.StatusPaused)
			item.element = q.low.PushFront(item)
			q.notify()
			return item, true
		}
	}

	for _, item := range q.running {
		if item.Task.Priority == model.PriorityMedium {
			delete(q.running, item.Task.ID)
			item.Task.SetStatus(model.StatusPaused)
			item.element = q.medium.PushFront(item)
			q.notify()
			return item, true
		}
	}

	return nil, false
}
