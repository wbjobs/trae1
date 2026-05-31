using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IWorkOrderRepository
{
    Task<IEnumerable<WorkOrder>> GetAllAsync();
    Task<WorkOrder?> GetByIdAsync(int id);
    Task<IEnumerable<WorkOrder>> GetByDeviceIdAsync(int deviceId);
    Task<WorkOrder> AddAsync(WorkOrder workOrder);
    Task<WorkOrder> UpdateAsync(WorkOrder workOrder);
    Task<IEnumerable<WorkOrder>> GetByStatusAsync(WorkOrderStatus status);
    Task<int> GetPendingCountAsync();
}
