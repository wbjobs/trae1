using Dapper;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Logging;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Infrastructure.Persistence;

public class OfflineRepository : IOfflineRepository
{
    private readonly ILogger<OfflineRepository> _logger;
    private readonly string _connectionString;

    public OfflineRepository(ILogger<OfflineRepository> logger, string databasePath = "gateway_data.db")
    {
        _logger = logger;
        _connectionString = $"Data Source={databasePath}";
    }

    public async Task InitializeAsync()
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                CREATE TABLE IF NOT EXISTS OfflineMessages (
                    Id INTEGER PRIMARY KEY AUTOINCREMENT,
                    DeviceId TEXT NOT NULL,
                    PointId TEXT NOT NULL,
                    PointName TEXT NOT NULL,
                    Value REAL NOT NULL,
                    Unit TEXT,
                    Timestamp TEXT NOT NULL,
                    Quality INTEGER NOT NULL,
                    DataType TEXT,
                    RetryCount INTEGER DEFAULT 0,
                    IsSent INTEGER DEFAULT 0,
                    CreatedAt TEXT NOT NULL,
                    SentAt TEXT
                );
                CREATE INDEX IF NOT EXISTS IX_OfflineMessages_IsSent ON OfflineMessages(IsSent);
                CREATE INDEX IF NOT EXISTS IX_OfflineMessages_DeviceId ON OfflineMessages(DeviceId);
            ");

            _logger.LogInformation("Offline repository initialized");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to initialize offline repository");
            throw;
        }
    }

    public async Task SaveMessageAsync(OfflineMessage message)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                INSERT INTO OfflineMessages 
                (DeviceId, PointId, PointName, Value, Unit, Timestamp, Quality, DataType, RetryCount, IsSent, CreatedAt)
                VALUES (@DeviceId, @PointId, @PointName, @Value, @Unit, @Timestamp, @Quality, @DataType, @RetryCount, @IsSent, @CreatedAt);
            ", message);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to save offline message for point {PointId}", message.PointId);
        }
    }

    public async Task SaveMessagesBatchAsync(IEnumerable<OfflineMessage> messages)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            using var transaction = await connection.BeginTransactionAsync();

            foreach (var message in messages)
            {
                await connection.ExecuteAsync(@"
                    INSERT INTO OfflineMessages 
                    (DeviceId, PointId, PointName, Value, Unit, Timestamp, Quality, DataType, RetryCount, IsSent, CreatedAt)
                    VALUES (@DeviceId, @PointId, @PointName, @Value, @Unit, @Timestamp, @Quality, @DataType, @RetryCount, @IsSent, @CreatedAt);
                ", message, transaction);
            }

            await transaction.CommitAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to save batch of offline messages");
        }
    }

    public async Task<IEnumerable<OfflineMessage>> GetPendingMessagesAsync(int batchSize = 100)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var messages = await connection.QueryAsync<OfflineMessage>(@"
                SELECT * FROM OfflineMessages 
                WHERE IsSent = 0 
                ORDER BY CreatedAt ASC 
                LIMIT @BatchSize;
            ", new { BatchSize = batchSize });

            return messages;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to retrieve pending offline messages");
            return Enumerable.Empty<OfflineMessage>();
        }
    }

    public async Task MarkAsSentAsync(long id)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                UPDATE OfflineMessages 
                SET IsSent = 1, SentAt = @SentAt 
                WHERE Id = @Id;
            ", new { Id = id, SentAt = DateTime.UtcNow });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to mark offline message {Id} as sent", id);
        }
    }

    public async Task MarkBatchAsSentAsync(IEnumerable<long> ids)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            using var transaction = await connection.BeginTransactionAsync();

            foreach (var id in ids)
            {
                await connection.ExecuteAsync(@"
                    UPDATE OfflineMessages 
                    SET IsSent = 1, SentAt = @SentAt 
                    WHERE Id = @Id;
                ", new { Id = id, SentAt = DateTime.UtcNow }, transaction);
            }

            await transaction.CommitAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to mark batch of offline messages as sent");
        }
    }

    public async Task<int> GetPendingCountAsync()
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var count = await connection.ExecuteScalarAsync<int>(@"
                SELECT COUNT(*) FROM OfflineMessages WHERE IsSent = 0;
            ");

            return count;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to get pending offline message count");
            return 0;
        }
    }

    public async Task CleanupOldMessagesAsync(int daysOld = 30)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var cutoffDate = DateTime.UtcNow.AddDays(-daysOld);

            var deleted = await connection.ExecuteAsync(@"
                DELETE FROM OfflineMessages 
                WHERE IsSent = 1 AND CreatedAt < @CutoffDate;
            ", new { CutoffDate = cutoffDate });

            if (deleted > 0)
            {
                _logger.LogInformation("Cleaned up {Count} old offline messages", deleted);
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to cleanup old offline messages");
        }
    }
}