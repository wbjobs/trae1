# start.ps1
# 开发环境启动脚本

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Audio Spectrum Analyzer" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$ErrorActionPreference = "Continue"

Write-Host "[INFO] Installing dependencies..." -ForegroundColor Green
npm install

Write-Host ""
Write-Host "[INFO] Starting development servers..." -ForegroundColor Green
Write-Host "[INFO] Frontend: http://localhost:3000" -ForegroundColor Yellow
Write-Host "[INFO] Backend:  http://localhost:8080" -ForegroundColor Yellow
Write-Host ""

npm run dev
