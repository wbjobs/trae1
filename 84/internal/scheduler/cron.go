package scheduler

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/robfig/cron/v3"
	"go.uber.org/zap"
)

type SnapshotTask struct {
	ID          string
	Name        string
	CronExpr    string
	Images      []string
	Database    *DatabaseConfig
	Incremental bool
	GroupID     string
	Enabled     bool
	LastRun     *time.Time
	NextRun     *time.Time
	CreatedAt   time.Time
	QuickFreeze bool
}

type DatabaseConfig struct {
	Type     string
	Host     string
	Port     int
	User     string
	Password string
	Database string
	PodName  string
}

type TaskExecutor interface {
	ExecuteSnapshot(ctx context.Context, task *SnapshotTask) error
}

type Scheduler struct {
	cron       *cron.Cron
	logger     *zap.Logger
	tasks      map[string]*SnapshotTask
	entries    map[string]cron.EntryID
	executor   TaskExecutor
	mu         sync.RWMutex
	running    bool
}

func NewScheduler(logger *zap.Logger, executor TaskExecutor) *Scheduler {
	return &Scheduler{
		cron:     cron.New(cron.WithSeconds()),
		logger:   logger,
		tasks:    make(map[string]*SnapshotTask),
		entries:  make(map[string]cron.EntryID),
		executor: executor,
	}
}

func (s *Scheduler) Start() {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.running {
		return
	}

	s.cron.Start()
	s.running = true
	s.logger.Info("Snapshot scheduler started")
}

func (s *Scheduler) Stop() {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running {
		return
	}

	ctx := s.cron.Stop()
	<-ctx.Done()
	s.running = false
	s.logger.Info("Snapshot scheduler stopped")
}

func (s *Scheduler) AddTask(task *SnapshotTask) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.tasks[task.ID]; exists {
		return fmt.Errorf("task with ID %s already exists", task.ID)
	}

	entryID, err := s.cron.AddFunc(task.CronExpr, func() {
		s.runTask(task)
	})
	if err != nil {
		return fmt.Errorf("failed to add cron task: %w", err)
	}

	entry := s.cron.Entry(entryID)
	task.NextRun = &entry.Schedule.Next(time.Now())

	s.tasks[task.ID] = task
	s.entries[task.ID] = entryID

	s.logger.Info("Snapshot task added",
		zap.String("task_id", task.ID),
		zap.String("name", task.Name),
		zap.String("cron", task.CronExpr),
	)

	return nil
}

func (s *Scheduler) RemoveTask(taskID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	entryID, exists := s.entries[taskID]
	if !exists {
		return fmt.Errorf("task with ID %s not found", taskID)
	}

	s.cron.Remove(entryID)
	delete(s.tasks, taskID)
	delete(s.entries, taskID)

	s.logger.Info("Snapshot task removed",
		zap.String("task_id", taskID),
	)

	return nil
}

func (s *Scheduler) GetTask(taskID string) (*SnapshotTask, error) {
	s.mu.RLock()
	defer s.mu.RUnlock()

	task, exists := s.tasks[taskID]
	if !exists {
		return nil, fmt.Errorf("task with ID %s not found", taskID)
	}

	entryID := s.entries[taskID]
	entry := s.cron.Entry(entryID)
	task.NextRun = &entry.Next

	return task, nil
}

func (s *Scheduler) ListTasks() []*SnapshotTask {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var tasks []*SnapshotTask
	for _, task := range s.tasks {
		entryID := s.entries[task.ID]
		entry := s.cron.Entry(entryID)
		task.NextRun = &entry.Next
		tasks = append(tasks, task)
	}
	return tasks
}

func (s *Scheduler) PauseTask(taskID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	task, exists := s.tasks[taskID]
	if !exists {
		return fmt.Errorf("task with ID %s not found", taskID)
	}

	entryID := s.entries[taskID]
	s.cron.Remove(entryID)
	delete(s.entries, taskID)

	task.Enabled = false

	s.logger.Info("Snapshot task paused",
		zap.String("task_id", taskID),
	)

	return nil
}

func (s *Scheduler) ResumeTask(taskID string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	task, exists := s.tasks[taskID]
	if !exists {
		return fmt.Errorf("task with ID %s not found", taskID)
	}

	entryID, err := s.cron.AddFunc(task.CronExpr, func() {
		s.runTask(task)
	})
	if err != nil {
		return fmt.Errorf("failed to resume task: %w", err)
	}

	entry := s.cron.Entry(entryID)
	task.Enabled = true
	task.NextRun = &entry.Next
	s.entries[taskID] = entryID

	s.logger.Info("Snapshot task resumed",
		zap.String("task_id", taskID),
	)

	return nil
}

func (s *Scheduler) RunTaskNow(taskID string) error {
	s.mu.RLock()
	task, exists := s.tasks[taskID]
	s.mu.RUnlock()

	if !exists {
		return fmt.Errorf("task with ID %s not found", taskID)
	}

	s.logger.Info("Running snapshot task immediately",
		zap.String("task_id", taskID),
		zap.String("name", task.Name),
	)

	go s.runTask(task)
	return nil
}

func (s *Scheduler) runTask(task *SnapshotTask) {
	s.logger.Info("Executing scheduled snapshot task",
		zap.String("task_id", task.ID),
		zap.String("name", task.Name),
	)

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Minute)
	defer cancel()

	now := time.Now()
	task.LastRun = &now

	if err := s.executor.ExecuteSnapshot(ctx, task); err != nil {
		s.logger.Error("Scheduled snapshot task failed",
			zap.String("task_id", task.ID),
			zap.Error(err),
		)
		return
	}

	s.logger.Info("Scheduled snapshot task completed successfully",
		zap.String("task_id", task.ID),
	)
}

func (s *Scheduler) UpdateTaskCron(taskID, newCronExpr string) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	task, exists := s.tasks[taskID]
	if !exists {
		return fmt.Errorf("task with ID %s not found", taskID)
	}

	entryID := s.entries[taskID]
	s.cron.Remove(entryID)

	newEntryID, err := s.cron.AddFunc(newCronExpr, func() {
		s.runTask(task)
	})
	if err != nil {
		oldEntryID, addErr := s.cron.AddFunc(task.CronExpr, func() {
			s.runTask(task)
		})
		if addErr != nil {
			return fmt.Errorf("failed to update cron expression and restore old: %w", err)
		}
		s.entries[taskID] = oldEntryID
		return fmt.Errorf("failed to update cron expression: %w", err)
	}

	task.CronExpr = newCronExpr
	entry := s.cron.Entry(newEntryID)
	task.NextRun = &entry.Next
	s.entries[taskID] = newEntryID

	s.logger.Info("Task cron expression updated",
		zap.String("task_id", taskID),
		zap.String("new_cron", newCronExpr),
	)

	return nil
}
