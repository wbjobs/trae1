# build_wasm.ps1
# Emscripten WebAssembly 编译脚本

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Audio FFT WebAssembly Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 检查 Emscripten SDK
if (-not $env:EMSDK) {
    Write-Host ""
    Write-Host "[ERROR] EMSDK environment variable not found!" -ForegroundColor Red
    Write-Host "Please install Emscripten SDK:" -ForegroundColor Yellow
    Write-Host "  git clone https://github.com/emscripten-core/emsdk.git" -ForegroundColor White
    Write-Host "  cd emsdk" -ForegroundColor White
    Write-Host "  emsdk install latest" -ForegroundColor White
    Write-Host "  emsdk activate latest" -ForegroundColor White
    Write-Host "  emsdk_env.ps1" -ForegroundColor White
    exit 1
}

Write-Host "[INFO] Using Emscripten SDK at: $env:EMSDK" -ForegroundColor Green

# 源文件和输出路径
$srcDir = "cpp"
$outputDir = "public"
$srcFile = Join-Path $srcDir "fft.cpp"
$outputFile = Join-Path $outputDir "fft.js"

# 确保输出目录存在
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

Write-Host "[INFO] Compiling fft.cpp to WebAssembly..." -ForegroundColor Green

# 编译命令
$emccArgs = @(
    $srcFile,
    "-o", $outputFile,
    "-O3",
    "-lembind",
    "-s", "WASM=1",
    "-s", "EXPORT_ES6=1",
    "-s", "MODULARIZE=1",
    "-s", "EXPORT_NAME=FFTModule",
    "-s", "ENVIRONMENT=web,worker",
    "-s", "ALLOW_MEMORY_GROWTH=1",
    "-s", "MAXIMUM_MEMORY=512MB",
    "-s", "EXPORTED_RUNTIME_METHODS=['ccall','cwrap']",
    "--bind"
)

& "$env:EMSDK\upstream\emscripten\em++.bat" @emccArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "[SUCCESS] WebAssembly build complete!" -ForegroundColor Green
    Write-Host "  Output: $outputFile" -ForegroundColor White
    
    if (Test-Path (Join-Path $outputDir "fft.wasm")) {
        Write-Host "  WASM:   $(Join-Path $outputDir 'fft.wasm')" -ForegroundColor White
    }
} else {
    Write-Host ""
    Write-Host "[ERROR] Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Build Complete" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
