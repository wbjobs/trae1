using System.Net;
using System.Net.Mail;
using System.Text;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace OpcUaGateway.Application.Services;

public class EmailNotificationService : IEmailNotificationService
{
    private readonly ILogger<EmailNotificationService> _logger;
    private readonly EmailConfig _emailConfig;

    public EmailNotificationService(
        ILogger<EmailNotificationService> logger,
        IOptions<GatewayConfig> config)
    {
        _logger = logger;
        _emailConfig = config.Value.Email;
    }

    public async Task SendCertificateChangedNotificationAsync(
        string deviceId,
        string deviceName,
        CertificateInfo oldCert,
        CertificateInfo newCert,
        bool isAutoAccepted)
    {
        if (!_emailConfig.EnableNotifications || _emailConfig.AdminEmails.Count == 0)
        {
            _logger.LogDebug("Email notifications disabled or no admin emails configured");
            return;
        }

        var subject = $"[OPC UA Gateway] Certificate Changed - {deviceName} ({deviceId})";
        var body = new StringBuilder();
        body.AppendLine($"Device: {deviceName} ({deviceId})");
        body.AppendLine($"Event: Server certificate has been renewed/changed");
        body.AppendLine();
        body.AppendLine("=== Old Certificate ===");
        body.AppendLine($"Subject: {oldCert.Subject}");
        body.AppendLine($"Thumbprint: {oldCert.Thumbprint}");
        body.AppendLine($"Expiry: {oldCert.NotAfter:yyyy-MM-dd HH:mm:ss UTC}");
        body.AppendLine();
        body.AppendLine("=== New Certificate ===");
        body.AppendLine($"Subject: {newCert.Subject}");
        body.AppendLine($"Thumbprint: {newCert.Thumbprint}");
        body.AppendLine($"Expiry: {newCert.NotAfter:yyyy-MM-dd HH:mm:ss UTC}");
        body.AppendLine($"Issuer: {newCert.Issuer}");
        body.AppendLine();
        body.AppendLine($"Auto-accepted: {isAutoAccepted}");
        body.AppendLine($"Time: {DateTime.UtcNow:yyyy-MM-dd HH:mm:ss UTC}");
        body.AppendLine();
        body.AppendLine("This is an automated message from OPC UA Data Collection Gateway.");

        await SendEmailAsync(subject, body.ToString());
    }

    public async Task SendCertificateExpiryWarningAsync(
        string deviceId,
        string deviceName,
        CertificateInfo cert,
        int daysUntilExpiry)
    {
        if (!_emailConfig.EnableNotifications || _emailConfig.AdminEmails.Count == 0)
        {
            _logger.LogDebug("Email notifications disabled or no admin emails configured");
            return;
        }

        var subject = $"[OPC UA Gateway] Certificate Expiry Warning - {deviceName}";
        var body = new StringBuilder();
        body.AppendLine($"Device: {deviceName} ({deviceId})");
        body.AppendLine($"Certificate expires in {daysUntilExpiry} days");
        body.AppendLine($"Subject: {cert.Subject}");
        body.AppendLine($"Thumbprint: {cert.Thumbprint}");
        body.AppendLine($"Expiry Date: {cert.NotAfter:yyyy-MM-dd HH:mm:ss UTC}");
        body.AppendLine($"Issuer: {cert.Issuer}");
        body.AppendLine();
        body.AppendLine("Please renew the certificate before it expires to avoid connection failures.");
        body.AppendLine();
        body.AppendLine("This is an automated message from OPC UA Data Collection Gateway.");

        await SendEmailAsync(subject, body.ToString());
    }

    public async Task SendCertificateRejectedNotificationAsync(
        string deviceId,
        string deviceName,
        CertificateInfo cert,
        string reason)
    {
        if (!_emailConfig.EnableNotifications || _emailConfig.AdminEmails.Count == 0)
        {
            _logger.LogDebug("Email notifications disabled or no admin emails configured");
            return;
        }

        var subject = $"[OPC UA Gateway] Certificate Rejected - {deviceName} ({deviceId})";
        var body = new StringBuilder();
        body.AppendLine($"Device: {deviceName} ({deviceId})");
        body.AppendLine($"Certificate was rejected during connection attempt");
        body.AppendLine($"Subject: {cert.Subject}");
        body.AppendLine($"Thumbprint: {cert.Thumbprint}");
        body.AppendLine($"Expiry: {cert.NotAfter:yyyy-MM-dd HH:mm:ss UTC}");
        body.AppendLine($"Reason: {reason}");
        body.AppendLine();
        body.AppendLine("To resolve this:");
        body.AppendLine("1. Verify the certificate is legitimate");
        body.AppendLine("2. If using WhitelistOnly strategy, add the certificate to the whitelist");
        body.AppendLine("3. If using ManualConfirmation strategy, manually approve the certificate");
        body.AppendLine();
        body.AppendLine("This is an automated message from OPC UA Data Collection Gateway.");

        await SendEmailAsync(subject, body.ToString());
    }

    private async Task SendEmailAsync(string subject, string body)
    {
        if (string.IsNullOrEmpty(_emailConfig.SmtpServer))
        {
            _logger.LogWarning("SMTP server not configured, skipping email: {Subject}", subject);
            return;
        }

        try
        {
            using var message = new MailMessage();
            message.From = new MailAddress(_emailConfig.FromAddress, _emailConfig.FromName);
            message.Subject = subject;
            message.Body = body;
            message.IsBodyHtml = false;

            foreach (var adminEmail in _emailConfig.AdminEmails)
            {
                if (!string.IsNullOrWhiteSpace(adminEmail))
                {
                    message.To.Add(adminEmail);
                }
            }

            if (message.To.Count == 0)
            {
                _logger.LogWarning("No valid admin email addresses");
                return;
            }

            using var smtpClient = new SmtpClient(_emailConfig.SmtpServer, _emailConfig.SmtpPort);
            smtpClient.EnableSsl = _emailConfig.UseSsl;

            if (!string.IsNullOrEmpty(_emailConfig.Username))
            {
                smtpClient.Credentials = new NetworkCredential(
                    _emailConfig.Username,
                    _emailConfig.Password ?? string.Empty);
            }

            await smtpClient.SendMailAsync(message);
            _logger.LogInformation("Email notification sent: {Subject}", subject);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to send email notification: {Subject}", subject);
        }
    }
}