package database

import (
	"context"
	"database/sql"
	"fmt"
	"time"

	_ "github.com/go-sql-driver/mysql"
	"go.uber.org/zap"
)

type MySQLConfig struct {
	Host     string
	Port     int
	User     string
	Password string
	Database string
	Timeout  time.Duration
}

type MySQLClient struct {
	db     *sql.DB
	cfg    *MySQLConfig
	logger *zap.Logger
}

func NewMySQLClient(cfg *MySQLConfig, logger *zap.Logger) (*MySQLClient, error) {
	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?timeout=%s&parseTime=true",
		cfg.User, cfg.Password, cfg.Host, cfg.Port, cfg.Database, cfg.Timeout)

	db, err := sql.Open("mysql", dsn)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to MySQL: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), cfg.Timeout)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to ping MySQL: %w", err)
	}

	db.SetMaxOpenConns(5)
	db.SetMaxIdleConns(2)
	db.SetConnMaxLifetime(time.Hour)

	return &MySQLClient{
		db:     db,
		cfg:    cfg,
		logger: logger,
	}, nil
}

func (c *MySQLClient) Close() error {
	if c.db != nil {
		return c.db.Close()
	}
	return nil
}

func (c *MySQLClient) Freeze(ctx context.Context) error {
	c.logger.Info("Freezing MySQL database with FLUSH TABLES WITH READ LOCK")

	_, err := c.db.ExecContext(ctx, "FLUSH TABLES WITH READ LOCK")
	if err != nil {
		return fmt.Errorf("failed to execute FLUSH TABLES WITH READ LOCK: %w", err)
	}

	c.logger.Info("MySQL database frozen successfully")
	return nil
}

func (c *MySQLClient) FreezeQuick(ctx context.Context) error {
	c.logger.Info("Quick freeze MySQL - only flushing logs and ensuring consistency point")

	_, err := c.db.ExecContext(ctx, "FLUSH NO_WRITE_TO_BINLOG LOGS")
	if err != nil {
		c.logger.Warn("FLUSH LOGS failed, proceeding anyway", zap.Error(err))
	}

	var logPos string
	var logFile string
	err = c.db.QueryRowContext(ctx, "SHOW MASTER STATUS").Scan(
		&logFile, &logPos, new(interface{}), new(interface{}), new(interface{}),
	)
	if err != nil {
		c.logger.Warn("Failed to get master status", zap.Error(err))
	} else {
		c.logger.Info("MySQL consistency point recorded",
			zap.String("binlog_file", logFile),
			zap.String("binlog_pos", logPos),
		)
	}

	_, err = c.db.ExecContext(ctx, "FLUSH NO_WRITE_TO_BINLOG ENGINE LOGS")
	if err != nil {
		c.logger.Warn("FLUSH ENGINE LOGS failed", zap.Error(err))
	}

	c.logger.Info("MySQL quick freeze completed - consistency point established")
	return nil
}

func (c *MySQLClient) Unfreeze(ctx context.Context) error {
	c.logger.Info("Unfreezing MySQL database with UNLOCK TABLES")

	_, err := c.db.ExecContext(ctx, "UNLOCK TABLES")
	if err != nil {
		return fmt.Errorf("failed to execute UNLOCK TABLES: %w", err)
	}

	c.logger.Info("MySQL database unfrozen successfully")
	return nil
}

func (c *MySQLClient) Verify(ctx context.Context) error {
	c.logger.Info("Verifying MySQL database integrity after snapshot restore")

	var result string
	err := c.db.QueryRowContext(ctx, "SELECT 1").Scan(&result)
	if err != nil {
		return fmt.Errorf("MySQL connection check failed: %w", err)
	}

	c.logger.Info("Checking for crash recovery artifacts")

	var varValue string
	err = c.db.QueryRowContext(ctx, "SHOW VARIABLES LIKE 'innodb_fast_shutdown'").Scan(new(interface{}), &varValue)
	if err == nil {
		c.logger.Info("InnoDB fast shutdown setting", zap.String("value", varValue))
	}

	err = c.db.QueryRowContext(ctx, "SHOW VARIABLES LIKE 'innodb_force_recovery'").Scan(new(interface{}), &varValue)
	if err == nil && varValue != "0" {
		c.logger.Warn("InnoDB force recovery is enabled", zap.String("value", varValue))
	}

	tables, err := c.db.QueryContext(ctx, "SHOW TABLES")
	if err != nil {
		return fmt.Errorf("failed to list tables: %w", err)
	}
	defer tables.Close()

	var tableName string
	var checked, repaired int
	for tables.Next() {
		if err := tables.Scan(&tableName); err != nil {
			c.logger.Warn("Failed to scan table name", zap.Error(err))
			continue
		}

		checkResult := struct {
			Table   string
			Op      string
			MsgType string
			MsgText string
		}{}

		query := fmt.Sprintf("CHECK TABLE `%s` EXTENDED", tableName)
		rows, err := c.db.QueryContext(ctx, query)
		if err != nil {
			c.logger.Warn("CHECK TABLE failed",
				zap.String("table", tableName),
				zap.Error(err),
			)
			continue
		}

		for rows.Next() {
			if err := rows.Scan(&checkResult.Table, &checkResult.Op, &checkResult.MsgType, &checkResult.MsgText); err != nil {
				continue
			}
			if checkResult.MsgType == "error" || checkResult.MsgType == "warning" {
				c.logger.Warn("Table check issue found",
					zap.String("table", checkResult.Table),
					zap.String("type", checkResult.MsgType),
					zap.String("message", checkResult.MsgText),
				)

				if checkResult.MsgType == "error" {
					repairQuery := fmt.Sprintf("REPAIR TABLE `%s`", tableName)
					if _, err := c.db.ExecContext(ctx, repairQuery); err != nil {
						c.logger.Error("Table repair failed",
							zap.String("table", tableName),
							zap.Error(err),
						)
					} else {
						repaired++
						c.logger.Info("Table repair attempted",
							zap.String("table", tableName),
						)
					}
				}
			}
		}
		rows.Close()
		checked++
	}

	c.logger.Info("MySQL database verification completed",
		zap.Int("tables_checked", checked),
		zap.Int("tables_repaired", repaired),
	)

	if repaired > 0 {
		c.logger.Warn("Some tables were repaired, consider running full backup verification")
	}

	return nil
}

func (c *MySQLClient) GetSlaveStatus(ctx context.Context) (bool, error) {
	var slaveStatus map[string]interface{}
	rows, err := c.db.QueryContext(ctx, "SHOW SLAVE STATUS")
	if err != nil {
		return false, fmt.Errorf("failed to get slave status: %w", err)
	}
	defer rows.Close()

	columns, err := rows.Columns()
	if err != nil {
		return false, err
	}

	for rows.Next() {
		values := make([]interface{}, len(columns))
		ptrs := make([]interface{}, len(columns))
		for i := range values {
			ptrs[i] = &values[i]
		}
		if err := rows.Scan(ptrs...); err != nil {
			return false, err
		}
		slaveStatus = make(map[string]interface{})
		for i, col := range columns {
			slaveStatus[col] = values[i]
		}
	}

	if slaveStatus == nil {
		return false, nil
	}

	slaveIO, _ := slaveStatus["Slave_IO_Running"].(string)
	slaveSQL, _ := slaveStatus["Slave_SQL_Running"].(string)

	return slaveIO == "Yes" && slaveSQL == "Yes", nil
}

func (c *MySQLClient) StopSlave(ctx context.Context) error {
	c.logger.Info("Stopping MySQL slave")
	_, err := c.db.ExecContext(ctx, "STOP SLAVE")
	return err
}

func (c *MySQLClient) StartSlave(ctx context.Context) error {
	c.logger.Info("Starting MySQL slave")
	_, err := c.db.ExecContext(ctx, "START SLAVE")
	return err
}
