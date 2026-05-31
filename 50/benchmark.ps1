param(
    [int]$FileSizeMB = 100,
    [int]$Port = 9876,
    [string]$ServerAddr = "127.0.0.1"
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path $MyInvocation.MyCommand.Path
$BinDir = "$ProjectDir\target\release"
$Exe = "$BinDir\quic-transfer.exe"
$TestDir = "$ProjectDir\test_output"
$ServerOutDir = "$TestDir\received"
$ClientTempDir = "$TestDir\client_temp"
$TestFile = "$TestDir\testfile_${FileSizeMB}MB.bin"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  QUIC Transfer Throughput Benchmark" -ForegroundColor Cyan
Write-Host "  BBR vs CUBIC vs RENO Congestion Control" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $Exe)) {
    Write-Host "Building release binary..." -ForegroundColor Yellow
    Push-Location $ProjectDir
    cargo build --release 2>&1 | Out-Null
    Pop-Location
    Write-Host "Build complete." -ForegroundColor Green
}

Write-Host "Setup: Creating test file ($FileSizeMB MB)..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $TestDir | Out-Null
New-Item -ItemType Directory -Force -Path $ServerOutDir | Out-Null
New-Item -ItemType Directory -Force -Path $ClientTempDir | Out-Null

if (-not (Test-Path $TestFile)) {
    $fileStream = [System.IO.File]::Create($TestFile)
    $fileStream.SetLength($FileSizeMB * 1MB)
    $fileStream.Close()
    Write-Host "  Created: $TestFile ($FileSizeMB MB)" -ForegroundColor Green
} else {
    Write-Host "  Using existing: $TestFile" -ForegroundColor Green
}

$algorithms = @("bbr", "cubic", "reno")
$results = @{}

foreach ($algo in $algorithms) {
    Write-Host ""
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Write-Host "  Testing with --congestion $algo" -ForegroundColor Cyan
    Write-Host "----------------------------------------" -ForegroundColor Cyan

    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $serverLog = "$TestDir\server_${algo}_$timestamp.log"

    Write-Host "  Starting server ($algo)..." -ForegroundColor Yellow
    $serverProc = Start-Process -FilePath $Exe `
        -ArgumentList "receive", "--bind", "127.0.0.1", "--port", $Port, `
        "--output-dir", $ServerOutDir, "--congestion", $algo `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError $serverLog `
        -PassThru -NoNewWindow

    Start-Sleep -Seconds 2

    if ($serverProc.HasExited) {
        Write-Host "  Server failed to start! Check log: $serverLog" -ForegroundColor Red
        Get-Content $serverLog
        continue
    }

    Write-Host "  Sending file..." -ForegroundColor Yellow
    $clientLog = "$TestDir\client_${algo}_$timestamp.log"
    $clientOutput = & $Exe send "--ip" $ServerAddr "--port" $Port `
        "--files" $TestFile "--temp-dir" $ClientTempDir "--congestion" $algo 2>&1
    $clientOutput | Out-File -FilePath $clientLog

    $receivedFile = "$ServerOutDir\testfile_${FileSizeMB}MB.bin"
    if (Test-Path $receivedFile) {
        $receivedSize = (Get-Item $receivedFile).Length
        $expectedSize = $FileSizeMB * 1MB
        if ($receivedSize -eq $expectedSize) {
            Write-Host "  PASS: File size matches ($receivedSize bytes)" -ForegroundColor Green
        } else {
            Write-Host "  WARN: File size mismatch ($receivedSize vs $expectedSize)" -ForegroundColor Yellow
        }
        Remove-Item $receivedFile -Force
    } else {
        Write-Host "  WARN: Received file not found!" -ForegroundColor Yellow
    }

    Write-Host "  Stopping server..." -ForegroundColor Yellow
    Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Benchmark Complete" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Log files in: $TestDir"
Write-Host ""
Write-Host "To simulate high packet loss (5%+), use Clumsy or similar tool:"
Write-Host "  https://jagt.github.io/clumsy/"
Write-Host ""
Write-Host "Or use Windows firewall to simulate:"
Write-Host "  New-NetFirewallRule -DisplayName 'Drop 5%' -Action Block -Direction Outbound -Protocol UDP -RemotePort $Port"
Write-Host ""
