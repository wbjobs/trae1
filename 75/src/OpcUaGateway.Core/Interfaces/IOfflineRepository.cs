using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface IOfflineRepository
{
    Task InitializeAsync();
    Task SaveMessageAsync(OfflineMessage message);
    Task SaveMessagesBatchAsync(IEnumerable<OfflineMessage> messages);
    Task<IEnumerable<OfflineMessage>> GetPendingMessagesAsync(int batchSize = 100);
    Task MarkAsSentAsync(long id);
    Task MarkBatchAsSentAsync(IEnumerable<long> ids);
    Task<int> GetPendingCountAsync();
    Task CleanupOldMessagesAsync(int daysOld = 30);
}