using IndustrialDataCollector.Api;
using IndustrialDataCollector.Api.Services;
using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Infrastructure.Repositories;
using IndustrialDataCollector.Infrastructure.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddRazorPages();
builder.Services.AddServerSideBlazor();

builder.Services.Configure<InfluxDbSettings>(
    builder.Configuration.GetSection("InfluxDB"));

builder.Services.AddSingleton<IDeviceRepository, DeviceRepository>();
builder.Services.AddSingleton<IOfflineLogRepository, OfflineLogRepository>();
builder.Services.AddSingleton<IAlarmRepository, AlarmRepository>();
builder.Services.AddSingleton<IWorkOrderRepository, WorkOrderRepository>();
builder.Services.AddSingleton<DeviceService>();
builder.Services.AddSingleton<AlarmService>();
builder.Services.AddSingleton<IDeviceApiService, DeviceApiService>();
builder.Services.AddSingleton<IAlarmApiService, AlarmApiService>();
builder.Services.AddSingleton<IDataStorage, InfluxDbStorage>();
builder.Services.AddSingleton<ModbusService>();

builder.Services.AddControllers();

builder.Services.AddHostedService<DataCollectionService>();

var app = builder.Build();

using (var scope = app.Services.CreateScope())
{
    var services = scope.ServiceProvider;
    try
    {
        await SeedData.InitializeAsync(services);
    }
    catch (Exception ex)
    {
        var logger = services.GetRequiredService<ILogger<Program>>();
        logger.LogError(ex, "An error occurred seeding the DB.");
    }
}

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error");
    app.UseHsts();
}

app.UseHttpsRedirection();
app.UseStaticFiles();
app.UseRouting();

app.MapBlazorHub();
app.MapFallbackToPage("/_Host");
app.MapControllers();

app.Run();
