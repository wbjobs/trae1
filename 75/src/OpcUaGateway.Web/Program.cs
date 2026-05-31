using OpcUaGateway.Application.BackgroundTasks;
using OpcUaGateway.Application.Services;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Infrastructure.Mqtt;
using OpcUaGateway.Infrastructure.OpcUa;
using OpcUaGateway.Infrastructure.Persistence;

var builder = WebApplication.CreateBuilder(args);

builder.Services.Configure<GatewayConfig>(
    builder.Configuration.GetSection("Gateway"));

builder.Services.AddRazorPages();
builder.Services.AddServerSideBlazor();

builder.Services.AddHttpContextAccessor();

builder.Services.AddScoped(sp =>
{
    var navigationManager = sp.GetRequiredService<NavigationManager>();
    return new HttpClient { BaseAddress = new Uri(navigationManager.BaseUri) };
});

builder.Services.AddControllers();
builder.Services.AddSwaggerGen(c =>
{
    c.SwaggerDoc("v1", new() { Title = "OPC UA Gateway API", Version = "v1" });
});

builder.Services.AddSingleton<IOfflineRepository>(sp =>
{
    var logger = sp.GetRequiredService<ILogger<OfflineRepository>>();
    var config = sp.GetRequiredService<IOptions<GatewayConfig>>().Value;
    return new OfflineRepository(logger, config.DatabasePath);
});

builder.Services.AddSingleton<IMqttClientService, MqttClientService>();
builder.Services.AddTransient<IOpcUaClient, OpcUaClientService>();
builder.Services.AddSingleton<IThresholdAlertService, ThresholdAlertService>();
builder.Services.AddSingleton<IConfigService, ConfigService>();
builder.Services.AddSingleton<ICertificateManager, CertificateManagerService>();
builder.Services.AddSingleton<IEmailNotificationService, EmailNotificationService>();
builder.Services.AddSingleton<IRuleEngine, RuleEngineService>();
builder.Services.AddSingleton<IRuleService, RuleService>();
builder.Services.AddSingleton<IDataCollectionService, DataCollectionService>();
builder.Services.AddHostedService<DataCollectionBackgroundService>();

var app = builder.Build();

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
app.UseSwagger();
app.UseSwaggerUI(c =>
{
    c.SwaggerEndpoint("/swagger/v1/swagger.json", "OPC UA Gateway API v1");
});

app.Run();