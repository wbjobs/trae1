package logger

import (
	"context"
	"os"
	"time"

	"github.com/olivere/elastic/v7"
	"github.com/sirupsen/logrus"

	"transcode-gateway/internal/config"
)

type Logger struct {
	*logrus.Logger
	esClient *elastic.Client
	esIndex  string
	esOn     bool
}

func New(cfg *config.Config) *Logger {
	l := logrus.New()
	l.SetOutput(os.Stdout)
	l.SetFormatter(&logrus.TextFormatter{
		FullTimestamp:   true,
		TimestampFormat: time.RFC3339Nano,
	})

	lg := &Logger{Logger: l}

	if cfg.Elasticsearch.Enabled && len(cfg.Elasticsearch.Addresses) > 0 {
		opts := []elastic.ClientOptionFunc{
			elastic.SetURL(cfg.Elasticsearch.Addresses...),
			elastic.SetSniff(false),
			elastic.SetHealthcheck(false),
		}
		if cfg.Elasticsearch.Username != "" {
			opts = append(opts, elastic.SetBasicAuth(cfg.Elasticsearch.Username, cfg.Elasticsearch.Password))
		}
		client, err := elastic.NewClient(opts...)
		if err != nil {
			l.Warnf("连接 Elasticsearch 失败，回退到标准输出: %v", err)
		} else {
			lg.esClient = client
			lg.esIndex = cfg.Elasticsearch.Index
			lg.esOn = true
			l.AddHook(&esHook{client: client, index: cfg.Elasticsearch.Index})
		}
	}
	return lg
}

type esEntry struct {
	Timestamp time.Time              `json:"@timestamp"`
	Level     string                 `json:"level"`
	Message   string                 `json:"message"`
	TaskID    string                 `json:"task_id,omitempty"`
	Extra     map[string]interface{} `json:"extra,omitempty"`
}

type esHook struct {
	client *elastic.Client
	index  string
}

func (h *esHook) Levels() []logrus.Level { return logrus.AllLevels }

func (h *esHook) Fire(entry *logrus.Entry) error {
	if h.client == nil {
		return nil
	}
	rec := esEntry{
		Timestamp: entry.Time,
		Level:     entry.Level.String(),
		Message:   entry.Message,
	}
	if tid, ok := entry.Data["task_id"].(string); ok {
		rec.TaskID = tid
	}
	if len(entry.Data) > 0 {
		rec.Extra = make(map[string]interface{})
		for k, v := range entry.Data {
			if k == "task_id" {
				continue
			}
			rec.Extra[k] = v
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	_, err := h.client.Index().Index(h.index).BodyJson(rec).Do(ctx)
	return err
}

func (l *Logger) TaskLog(taskID string) *logrus.Entry {
	return l.WithField("task_id", taskID)
}

func (l *Logger) Close() {
	if l.esClient != nil {
		l.esClient.Stop()
	}
}
