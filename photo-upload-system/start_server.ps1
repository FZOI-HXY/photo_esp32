# ESP32-CAM 照片上传服务器启动脚本
# 使用方法: 右键点击此文件，选择"使用 PowerShell 运行"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "   ESP32-CAM 照片上传服务器启动脚本" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# 切换到backend目录
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$backendPath = Join-Path $scriptPath "backend"
Set-Location $backendPath

# 检查Python是否安装
try {
    $pythonVersion = python --version 2>&1
    Write-Host "[信息] $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "[错误] 未检测到Python，请确保Python已安装并添加到环境变量" -ForegroundColor Red
    Read-Host "按回车键退出"
    exit 1
}

Write-Host ""

# 检查依赖是否安装
Write-Host "[信息] 检查依赖..." -ForegroundColor Yellow
$dependencies = @("flask", "cv2", "numpy", "PIL")
$missingDeps = @()

foreach ($dep in $dependencies) {
    try {
        python -c "import $dep" 2>&1 | Out-Null
    } catch {
        $missingDeps += $dep
    }
}

if ($missingDeps.Count -gt 0) {
    Write-Host "[警告] 缺少依赖: $($missingDeps -join ', ')，正在安装..." -ForegroundColor Yellow
    pip install -r requirements.txt
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[错误] 依赖安装失败" -ForegroundColor Red
        Read-Host "按回车键退出"
        exit 1
    }
}

Write-Host "[信息] 依赖检查完成" -ForegroundColor Green
Write-Host ""

# 创建uploads目录（如果不存在）
$uploadsPath = Join-Path $scriptPath "uploads"
if (-not (Test-Path $uploadsPath)) {
    New-Item -ItemType Directory -Path $uploadsPath | Out-Null
    Write-Host "[信息] 创建uploads目录" -ForegroundColor Green
}

# 获取本机IP地址
$ipAddress = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } | Select-Object -First 1).IPAddress
if (-not $ipAddress) {
    $ipAddress = "127.0.0.1"
}

# 启动服务器
Write-Host "[信息] 启动服务器..." -ForegroundColor Green
Write-Host "[信息] 服务器将在以下地址运行:" -ForegroundColor Green
Write-Host "       - http://$ipAddress`:5000" -ForegroundColor Cyan
Write-Host "       - http://127.0.0.1:5000" -ForegroundColor Cyan
Write-Host "[信息] 按Ctrl+C停止服务器" -ForegroundColor Yellow
Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

try {
    python app.py
} catch {
    Write-Host ""
    Write-Host "[警告] 服务器已停止或发生错误" -ForegroundColor Yellow
    Write-Host $_.Exception.Message -ForegroundColor Red
}

Write-Host ""
Read-Host "按回车键退出"
