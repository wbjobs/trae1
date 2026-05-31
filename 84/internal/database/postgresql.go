package database

import (
	"context"
	"database/sql"
	"fmt"
	"time"

	"github.com/lib/pq"
	"go.uber.org/zap"
)

type PostgreSQLConfig struct {
	Host     string
	Port     int
	User     string
	Password string
	Database string
	SSLMode  string
	Timeout  time.Duration
}

type PostgreSQLClient struct {
	db     *sql.DB
	cfg    *PostgreSQLConfig
	logger *zap.Logger
}

func NewPostgreSQLClient(cfg *PostgreSQLConfig, logger *zap.Logger) (*PostgreSQLClient, error) {
	connStr := fmt.Sprintf(
		"host=%s port=%d user=%s password=%s dbname=%s sslmode=%s connect_timeout=%d",
		cfg.Host, cfg.Port, cfg.User, cfg.Password, cfg.Database,
		cfg.SSLMode, int(cfg.Timeout.Seconds()),
	)

	db, err := sql.Open("postgres", connStr)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to PostgreSQL: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), cfg.Timeout)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to ping PostgreSQL: %w", err)
	}

	db.SetMaxOpenConns(5)
	db.SetMaxIdleConns(2)
	db.SetConnMaxLifetime(time.Hour)

	return &PostgreSQLClient{
		db:     db,
		cfg:    cfg,
		logger: logger,
	}, nil
}

func (c *PostgreSQLClient) Close() error {
	if c.db != nil {
		return c.db.Close()
	}
	return nil
}

func (c *PostgreSQLClient) Freeze(ctx context.Context) (string, error) {
	c.logger.Info("Starting PostgreSQL backup mode with pg_start_backup")

	var backupLabel string
	timestamp := time.Now().Format("20060102150405")
	backupLabel = fmt.Sprintf("rbd-snapshot-%s", timestamp)

	var lsn string
	err := c.db.QueryRowContext(ctx,
		"SELECT pg_start_backup($1, true, false)", backupLabel,
	).Scan(&lsn)
	if err != nil {
		return "", fmt.Errorf("failed to execute pg_start_backup: %w", err)
	}

	c.logger.Info("PostgreSQL backup mode started",
		zap.String("backup_label", backupLabel),
		zap.String("lsn", lsn),
	)
	return backupLabel, nil
}

func (c *PostgreSQLClient) FreezeQuick(ctx context.Context) (string, error) {
	c.logger.Info("Quick freeze PostgreSQL - switching WAL and creating checkpoint")

	var walName string
	err := c.db.QueryRowContext(ctx, "SELECT pg_switch_wal()").Scan(&walName)
	if err != nil {
		c.logger.Warn("pg_switch_wal failed, proceeding without WAL switch", zap.Error(err))
		walName = "unknown"
	}

	_, err = c.db.ExecContext(ctx, "CHECKPOINT")
	if err != nil {
		c.logger.Warn("CHECKPOINT failed", zap.Error(err))
	}

	var lsn string
	err = c.db.QueryRowContext(ctx, "SELECT pg_current_wal_lsn()").Scan(&lsn)
	if err != nil {
		c.logger.Warn("Failed to get current WAL LSN", zap.Error(err))
	}

	c.logger.Info("PostgreSQL quick freeze completed",
		zap.String("wal_file", walName),
		zap.String("checkpoint_lsn", lsn),
	)
	return lsn, nil
}

func (c *PostgreSQLClient) Unfreeze(ctx context.Context) error {
	c.logger.Info("Stopping PostgreSQL backup mode with pg_stop_backup")

	var lsn string
	err := c.db.QueryRowContext(ctx,
		"SELECT pg_stop_backup(false)",
	).Scan(&lsn)
	if err != nil {
		return fmt.Errorf("failed to execute pg_stop_backup: %w", err)
	}

	c.logger.Info("PostgreSQL backup mode stopped",
		zap.String("lsn", lsn),
	)
	return nil
}

func (c *PostgreSQLClient) Verify(ctx context.Context) error {
	c.logger.Info("Verifying PostgreSQL database integrity after snapshot restore")

	var result int
	err := c.db.QueryRowContext(ctx, "SELECT 1").Scan(&result)
	if err != nil {
		return fmt.Errorf("PostgreSQL connection check failed: %w", err)
	}

	c.logger.Info("Checking WAL and crash recovery status")

	var inRecovery bool
	if err := c.db.QueryRowContext(ctx, "SELECT pg_is_in_recovery()").Scan(&inRecovery); err == nil {
		if inRecovery {
			c.logger.Warn("Database is still in recovery mode - WAL replay may be incomplete")
		} else {
			c.logger.Info("Database is not in recovery mode - WAL replay completed")
		}
	}

	var lastCheckpoint string
	err = c.db.QueryRowContext(ctx, `
		SELECT checkpoint_start FROM pg_stat_checkpointer ORDER BY checkpoint_start DESC LIMIT 1
	`).Scan(&lastCheckpoint)
	if err == nil {
		c.logger.Info("Last checkpoint status", zap.String("checkpoint", lastCheckpoint))
	}

	rows, err := c.db.QueryContext(ctx, `
		SELECT schemaname, tablename 
		FROM pg_tables 
		WHERE schemaname NOT IN ('pg_catalog', 'information_schema')
	`)
	if err != nil {
		return fmt.Errorf("failed to list tables: %w", err)
	}
	defer rows.Close()

	type tableInfo struct {
		Schema string
		Table  string
	}

	var tables []tableInfo
	for rows.Next() {
		var ti tableInfo
		if err := rows.Scan(&ti.Schema, &ti.Table); err != nil {
			continue
		}
		tables = append(tables, ti)
	}

	var checked, reindexed int
	for _, ti := range tables {
		fullTableName := fmt.Sprintf(`"%s"."%s"`, ti.Schema, ti.Table)

		checkQuery := fmt.Sprintf("SELECT count(*) FROM %s", fullTableName)
		var count int
		if err := c.db.QueryRowContext(ctx, checkQuery).Scan(&count); err != nil {
			c.logger.Warn("Table access check failed",
				zap.String("table", fullTableName),
				zap.Error(err),
			)

			reindexQuery := fmt.Sprintf("REINDEX TABLE %s", fullTableName)
			if _, err := c.db.ExecContext(ctx, reindexQuery); err != nil {
				c.logger.Error("Table reindex failed",
					zap.String("table", fullTableName),
					zap.Error(err),
				)
			} else {
				reindexed++
				c.logger.Info("Table reindex attempted",
					zap.String("table", fullTableName),
				)
			}
		} else {
			checked++
		}

		analyzeQuery := fmt.Sprintf("ANALYZE %s", fullTableName)
		if _, err := c.db.ExecContext(ctx, analyzeQuery); err != nil {
			c.logger.Warn("Table analyze failed",
				zap.String("table", fullTableName),
				zap.Error(err),
			)
		}
	}

	c.logger.Info("PostgreSQL database verification completed",
		zap.Int("table_count", len(tables)),
		zap.Int("tables_checked", checked),
		zap.Int("tables_reindexed", reindexed),
	)

	if reindexed > 0 {
		c.logger.Warn("Some tables were reindexed, consider running a full VACUUM")
	}

	return nil
}

func (c *PostgreSQLClient) CheckReplicationSlots(ctx context.Context) ([]string, error) {
	rows, err := c.db.QueryContext(ctx, `
		SELECT slot_name FROM pg_replication_slots WHERE active = true
	`)
	if err != nil {
		return nil, fmt.Errorf("failed to check replication slots: %w", err)
	}
	defer rows.Close()

	var slots []string
	for rows.Next() {
		var slot string
		if err := rows.Scan(&slot); err != nil {
			continue
		}
		slots = append(slots, slot)
	}
	return slots, nil
}

func (c *PostgreSQLClient) IsInRecovery(ctx context.Context) (bool, error) {
	var inRecovery bool
	err := c.db.QueryRowContext(ctx, "SELECT pg_is_in_recovery()").Scan(&inRecovery)
	if err != nil {
		return false, fmt.Errorf("failed to check recovery status: %w", err)
	}
	return inRecovery, nil
}

func (c *PostgreSQLClient) GetWALPosition(ctx context.Context) (string, error) {
	inRecovery, err := c.IsInRecovery(ctx)
	if err != nil {
		return "", err
	}

	var lsn string
	if inRecovery {
		err = c.db.QueryRowContext(ctx, "SELECT pg_last_wal_receive_lsn()").Scan(&lsn)
	} else {
		err = c.db.QueryRowContext(ctx, "SELECT pg_current_wal_lsn()").Scan(&lsn)
	}
	if err != nil {
		return "", fmt.Errorf("failed to get WAL position: %w", err)
	}
	return lsn, nil
}

var _ = pq.QuoteLiteral
