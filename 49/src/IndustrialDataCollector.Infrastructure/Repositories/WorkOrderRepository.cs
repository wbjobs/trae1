using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;

namespace IndustrialDataCollector.Infrastructure.Repositories;

public class WorkOrderRepository : IWorkOrderRepository
{
    private static readonly List<WorkOrder> _workOrders = new();
    private static int _nextId = 1;
    private static readonly object _lock = new();

    public Task<IEnumerable<WorkOrder>> GetAllAsync()
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<WorkOrder>>(
                _workOrders.OrderByDescending(w => w.CreatedAt).ToList());
        }
    }

    public Task<WorkOrder?> GetByIdAsync(int id)
    {
        lock (_lock)
        {
            return Task.FromResult(_workOrders.FirstOrDefault(w => w.Id == id));
        }
    }

    public Task<IEnumerable<WorkOrder>> GetByDeviceIdAsync(int deviceId)
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<WorkOrder>>(
                _workOrders.Where(w => w.DeviceId == deviceId)
                           .OrderByDescending(w => w.CreatedAt).ToList());
        }
    }

    public Task<WorkOrder> AddAsync(WorkOrder workOrder)
    {
        lock (_lock)
        {
            workOrder.Id = _nextId++;
            workOrder.OrderNumber = $"WO-{DateTime.Now:yyyyMMdd}-{workOrder.Id:D4}";
            _workOrders.Add(workOrder);
            return Task.FromResult(workOrder);
        }
    }

    public Task<WorkOrder> UpdateAsync(WorkOrder workOrder)
    {
        lock (_lock)
        {
            var existing = _workOrders.FirstOrDefault(w => w.Id == workOrder.Id);
            if (existing == null)
                throw new KeyNotFoundException($"WorkOrder with ID {workOrder.Id} not found");

            existing.Status = workOrder.Status;
            existing.AssignedTo = workOrder.AssignedTo;
            existing.StartedAt = workOrder.StartedAt;
            existing.CompletedAt = workOrder.CompletedAt;
            existing.Resolution = workOrder.Resolution;
            existing.Description = workOrder.Description;

            return Task.FromResult(existing);
        }
    }

    public Task<IEnumerable<WorkOrder>> GetByStatusAsync(WorkOrderStatus status)
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<WorkOrder>>(
                _workOrders.Where(w => w.Status == status)
                           .OrderByDescending(w => w.CreatedAt).ToList());
        }
    }

    public Task<int> GetPendingCountAsync()
    {
        lock (_lock)
        {
            return Task.FromResult(
                _workOrders.Count(w => w.Status == WorkOrderStatus.Pending));
        }
    }
}
