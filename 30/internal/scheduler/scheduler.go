package scheduler

import (
	"context"
	"fmt"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/sirupsen/logrus"

	"transcode-gateway/internal/cgroup"
	"transcode-gateway/internal/config"
	"transcode-gateway/internal/logger"
	"transcode-gateway/internal/model"
	"transcode-gateway/internal/probe"
	"transcode-gateway/internal/queue"
	"transcode-gateway/internal/transcoder"
)

type Scheduler struct {
	cfg   *config.Config
	log   *logger.Logger
	trans *transcoder.Manager
	probe *probe.Monitor
	cgm   *cgroup.Manager
	queue *queue.PriorityQueue

	tasks map[string]*model.Task
	mu    sync.RWMutex

	stop     chan struct{}
	wg       sync.WaitGroup
	dispOnce sync.Once
}

func New(cfg *config.Config, log *logger.Logger, trans *transcoder.Manager, p *probe.Monitor, cgm *cgroup.Manager) *Scheduler {
	q := queue.New(cfg.Queue.MaxConcurrent, cfg.Queue.HighReserved, cfg.Queue.MediumReserved)
	return &Scheduler{
		cfg:   cfg,
		log:   log,
		trans: trans,
		probe: p,
		cgm:   cgm,
		queue: q,
		tasks: make(map[string]*model.Task),
		stop:  make(chan struct{}),
	}
}

func (s *Scheduler) Start(req model.TaskRequest) (*model.Task, error) {
	if _, ok := s.cfg.Profile(req.Profile); !ok {
		return nil, fmt.Errorf("未知转码档位: %s", req.Profile)
	}
	if req.Protocol != model.ProtoRTMP && req.Protocol != model.ProtoHLS && req.Protocol != model.ProtoSRT {
		return nil, fmt.Errorf("不支持的协议: %s", req.Protocol)
	}

	priority := model.PriorityMedium
	if req.Priority != "" {
		p, ok := model.ParsePriority(req.Priority)
		if !ok {
			return nil, fmt.Errorf("无效优先级: %s (可选: high/medium/low)", req.Priority)
		}
		priority = p
	}

	t := &model.Task{
		ID:        uuid.NewString(),
		Name:      req.Name,
		InputURL:  req.InputURL,
		Protocol:  req.Protocol,
		Profile:   req.Profile,
		Priority:  priority,
		Status:    model.StatusPending,
		CreatedAt: time.Now(),
	}

	s.mu.Lock()
	s.tasks[t.ID] = t
	s.mu.Unlock()

	if err := s.queue.Enqueue(t); err != nil {
		return nil, err
	}

	s.dispOnce.Do(func() {
		s.wg.Add(1)
		go s.dispatchLoop()
	})

	s.log.WithFields(logrus.Fields{
		"task_id":   t.ID,
		"name":      t.Name,
		"priority":  priority.String(),
		"protocol":  t.Protocol,
		"input_url": t.InputURL,
	}).Info("已创建转码任务并加入队列")
	return t, nil
}

func (s *Scheduler) Stop(taskID string) error {
	s.mu.RLock()
	t, ok := s.tasks[taskID]
	s.mu.RUnlock()
	if !ok {
		return fmt.Errorf("任务不存在: %s", taskID)
	}

	if s.queue.RemoveFromQueue(taskID) {
		t.SetStatus(model.StatusStopped)
		s.log.WithFields(logrus.Fields{"task_id": taskID}).Info("任务从队列中移除")
		return nil
	}

	if s.queue.IsRunning(taskID) {
		t.SetStatus(model.StatusStopped)
		if err := s.trans.Kill(taskID); err != nil {
			return err
		}
		if t.PID > 0 {
			s.cgm.ReleaseTask(t.PID)
		}
		s.queue.Complete(taskID)
		return nil
	}

	t.SetStatus(model.StatusStopped)
	return nil
}

func (s *Scheduler) List() []*model.Task {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]*model.Task, 0, len(s.tasks))
	for _, t := range s.tasks {
		out = append(out, t)
	}
	return out
}

func (s *Scheduler) Get(taskID string) (*model.Task, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	t, ok := s.tasks[taskID]
	return t, ok
}

func (s *Scheduler) QueueSnapshot() model.QueueSnapshot {
	return s.queue.Snapshot()
}

func (s *Scheduler) Close() {
	close(s.stop)
	s.mu.RLock()
	ids := make([]string, 0, len(s.tasks))
	for id := range s.tasks {
		ids = append(ids, id)
	}
	s.mu.RUnlock()
	for _, id := range ids {
		_ = s.trans.Kill(id)
	}
	s.wg.Wait()
	s.cgm.CleanupAll()
}

func (s *Scheduler) dispatchLoop() {
	defer s.wg.Done()

	for {
		select {
		case <-s.stop:
			return
		default:
		}

		item, ok := s.queue.Dequeue()
		if !ok {
			select {
			case <-s.queue.ArrivalCh():
			case <-s.stop:
				return
			case <-time.After(2 * time.Second):
			}
			continue
		}

		s.wg.Add(1)
		go func(it *queue.Item) {
			defer s.wg.Done()
			s.executeTask(it.Task)
		}(item)
	}
}

func (s *Scheduler) executeTask(t *model.Task) {
	defer s.queue.Complete(t.ID)

	runCtx, cancel := context.WithCancel(context.Background())
	defer cancel()

	probeTicker := time.NewTicker(time.Duration(s.cfg.StreamMonitor.ProbeInterval) * time.Second)
	defer probeTicker.Stop()

	go s.probeLoop(runCtx, t, probeTicker)

	disconnectNotified := false

	maxRetries := s.cfg.FFmpeg.MaxRetries
	if maxRetries <= 0 {
		maxRetries = 3
	}

	for {
		select {
		case <-s.stop:
			t.MarkStopped("服务关闭")
			_ = s.trans.Kill(t.ID)
			if t.PID > 0 {
				s.cgm.ReleaseTask(t.PID)
			}
			return
		default:
		}

		if t.GetStatus() == model.StatusStopped {
			return
		}

		if t.GetStatus() == model.StatusPaused {
			_ = s.trans.Kill(t.ID)
			if t.PID > 0 {
				s.cgm.ReleaseTask(t.PID)
				t.PID = 0
			}
			s.log.WithFields(logrus.Fields{"task_id": t.ID}).Info("任务被抢占，重新进入队列等待")
			return
		}

		if t.GetStatus() == model.StatusFailed {
			time.Sleep(500 * time.Millisecond)
			continue
		}

		if s.probe.Disconnected(t.ID) {
			if !disconnectNotified {
				disconnectNotified = true
				s.log.WithFields(logrus.Fields{
					"task_id": t.ID,
					"url":     t.InputURL,
				}).Warnf("输入流断开超过 %d 秒，主动 kill ffmpeg 进程", s.cfg.StreamMonitor.DisconnectTimeout)
				t.SetStatus(model.StatusRecovering)
				_ = s.trans.Kill(t.ID)
				if t.PID > 0 {
					s.cgm.ReleaseTask(t.PID)
					t.PID = 0
				}
			}
			select {
			case <-time.After(1 * time.Second):
			case <-s.stop:
				return
			}
			continue
		} else if disconnectNotified {
			disconnectNotified = false
			t.SetStatus(model.StatusRunning)
			s.log.WithFields(logrus.Fields{"task_id": t.ID}).Info("输入流恢复，自动重启转码")
		}

		if s.trans.IsRunning(t.ID) {
			s.queue.PreemptIfNeeded()
			time.Sleep(1 * time.Second)
			continue
		}

		s.queue.PreemptIfNeeded()

		err := s.runOnceWithRetries(runCtx, t, maxRetries)
		if err != nil {
			if t.GetStatus() == model.StatusStopped {
				return
			}
			if t.GetStatus() == model.StatusRecovering || t.GetStatus() == model.StatusPaused {
				time.Sleep(1 * time.Second)
				continue
			}
			s.log.WithFields(logrus.Fields{"task_id": t.ID}).Errorf("转码任务失败: %v", err)
			t.MarkFailed(err.Error())
			return
		}

		return
	}
}

func (s *Scheduler) runOnceWithRetries(runCtx context.Context, t *model.Task, maxRetries int) error {
	interval := time.Duration(s.cfg.FFmpeg.RetryInterval) * time.Second

	for attempt := 0; attempt <= maxRetries; attempt++ {
		if t.GetStatus() == model.StatusStopped {
			return nil
		}
		if attempt > 0 {
			t.RetryCount++
			s.log.WithFields(logrus.Fields{
				"task_id":     t.ID,
				"attempt":     attempt + 1,
				"retry_count": t.RetryCount,
			}).Warn("转码重试")
			select {
			case <-time.After(interval):
			case <-s.stop:
				return nil
			}
		}
		t.SetStatus(model.StatusRunning)
		t.StartedAt = time.Now()

		resCh, err := s.trans.Start(runCtx, t)
		if err != nil {
			s.log.WithFields(logrus.Fields{"task_id": t.ID}).Errorf("启动转码失败: %v", err)
			t.MarkFailed(err.Error())
			continue
		}

		if t.PID > 0 {
			if err := s.cgm.AssignTask(t.ID, t.PID, t.Priority); err != nil {
				s.log.WithFields(logrus.Fields{
					"task_id": t.ID,
					"pid":     t.PID,
				}).Warnf("cgroup 分配失败（不影响转码）: %v", err)
			}
		}

		res := <-resCh
		if t.PID > 0 {
			s.cgm.ReleaseTask(t.PID)
		}
		if res.Err == nil {
			return nil
		}

		if t.GetStatus() == model.StatusStopped {
			return nil
		}

		if !res.Retriable {
			return res.Err
		}

		s.log.WithFields(logrus.Fields{
			"task_id": t.ID,
			"error":   res.Err.Error(),
		}).Error("转码失败，准备重试")
	}
	return fmt.Errorf("已达最大重试次数 %d", maxRetries)
}

func (s *Scheduler) probeLoop(ctx context.Context, t *model.Task, ticker *time.Ticker) {
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			pctx, cancel := context.WithTimeout(ctx, time.Duration(s.cfg.StreamMonitor.ProbeTimeout)*time.Second)
			if err := s.probe.Probe(pctx, t); err != nil {
				s.log.WithFields(logrus.Fields{"task_id": t.ID}).Debugf("探测失败: %v", err)
			} else {
				t.LastProbeAt = time.Now()
			}
			cancel()
		}
	}
}

func (s *Scheduler) ZombieCleanupLoop() {
	t := time.NewTicker(time.Duration(s.cfg.ZombieCleanupInterval) * time.Second)
	defer t.Stop()
	for {
		select {
		case <-s.stop:
			return
		case <-t.C:
			n := s.trans.CleanupZombies()
			if n > 0 {
				s.log.Infof("僵尸进程清理，清理 %d 个残留句柄", n)
			}
		}
	}
}
