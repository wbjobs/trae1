package clickhouse

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/ClickHouse/clickhouse-go/v2"
	"github.com/ClickHouse/clickhouse-go/v2/lib/driver"
)

type Client struct {
	conn     driver.Conn
	database string
}

type SyncState struct {
	Table         string    `ch:"table"`
	ResumeToken  []byte    `ch:"resume_token"`
	LastUpdate   time.Time `ch:"last_update"`
	Processed    int64     `ch:"processed"`
	LagSeconds   float64   `ch:"lag_seconds"`
}

func NewClient(dsn, database string) (*Client, error) {
	ctx := context.Background()
	conn, err := clickhouse.Open(&clickhouse.Options{
		Addr: []string{"127.0.0.1:9000"},
		Auth: clickhouse.Auth{
			Database: database,
			Username: "default",
			Password: "",
		},
		Settings: clickhouse.Settings{
			"max_execution_time": 60,
		},
		Compression: &clickhouse.Compression{
			Method: clickhouse.CompressionLZ4,
		},
		DialTimeout:     time.Second * 30,
		MaxOpenConns:    10,
		MaxIdleConns:    5,
		ConnMaxLifetime: time.Hour,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to connect to ClickHouse: %w", err)
	}

	if err := conn.Ping(ctx); err != nil {
		return nil, fmt.Errorf("failed to ping ClickHouse: %w", err)
	}

	return &Client{conn: conn, database: database}, nil
}

func (c *Client) Close() error {
	return c.conn.Close()
}

func (c *Client) InitStateTable(ctx context.Context) error {
	query := `
	CREATE TABLE IF NOT EXISTS sync_state (
		table String,
		resume_token String,
		last_update DateTime64(3),
		processed Int64,
		lag_seconds Float64
	) ENGINE = ReplacingMergeTree()
	ORDER BY table
	`
	return c.conn.Exec(ctx, query)
}

func (c *Client) SaveResumeToken(ctx context.Context, table string, token []byte, processed int64, lag float64) error {
	query := `
	INSERT INTO sync_state (table, resume_token, last_update, processed, lag_seconds)
	VALUES (?, ?, ?, ?, ?)
	`
	return c.conn.Exec(ctx, query, table, string(token), time.Now(), processed, lag)
}

func (c *Client) GetResumeToken(ctx context.Context, table string) ([]byte, error) {
	query := `SELECT resume_token FROM sync_state WHERE table = ? ORDER BY last_update DESC LIMIT 1`
	row := c.conn.QueryRow(ctx, query, table)
	var token string
	err := row.Scan(&token)
	if err != nil {
		if err == clickhouse.ErrNoRows {
			return nil, nil
		}
		return nil, err
	}
	return []byte(token), nil
}

func (c *Client) GetStatus(ctx context.Context) ([]SyncState, error) {
	query := `SELECT table, resume_token, last_update, processed, lag_seconds FROM sync_state`
	rows, err := c.conn.Query(ctx, query)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var states []SyncState
	for rows.Next() {
		var state SyncState
		if err := rows.ScanStruct(&state); err != nil {
			return nil, err
		}
		states = append(states, state)
	}
	return states, nil
}

func (c *Client) Insert(ctx context.Context, table string, data map[string]interface{}) error {
	var cols []string
	var placeholders []string
	var vals []interface{}
	i := 1
	for k, v := range data {
		cols = append(cols, k)
		placeholders = append(placeholders, fmt.Sprintf("?$d", i))
		vals = append(vals, v)
		i++
	}

	query := fmt.Sprintf("INSERT INTO %s (%s) VALUES (%s)", table,
		strings.Join(cols, ", "), strings.Join(placeholders, ", "))
	return c.conn.Exec(ctx, query, vals...)
}

func (c *Client) BatchInsert(ctx context.Context, table string, rows []map[string]interface{}) error {
	if len(rows) == 0 {
		return nil
	}

	var cols []string
	for k := range rows[0] {
		cols = append(cols, k)
	}

	batch, err := c.conn.PrepareBatch(ctx, fmt.Sprintf("INSERT INTO %s (%s)", table, strings.Join(cols, ", ")))
	if err != nil {
		return err
	}

	for _, row := range rows {
		var vals []interface{}
		for _, col := range cols {
			vals = append(vals, row[col])
		}
		if err := batch.Append(vals...); err != nil {
			return err
		}
	}

	return batch.Send()
}

func (c *Client) Delete(ctx context.Context, table string, idField string, id interface{}) error {
	query := fmt.Sprintf("ALTER TABLE %s DELETE WHERE %s = ?", table, idField)
	return c.conn.Exec(ctx, query, id)
}
