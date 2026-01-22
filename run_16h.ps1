# run_16h.ps1
# 用法：在 D:\code\chess_train 執行
#   powershell -ExecutionPolicy Bypass -File .\run_16h.ps1

$ErrorActionPreference = "Stop"

$root = "D:\code\chess_train"
Set-Location $root

$trainer = Join-Path $root "build\trainer.exe"
if (!(Test-Path $trainer)) { throw "找不到 $trainer，請先 cmake --build build" }

# 日誌資料夾
$logDir = Join-Path $root ("logs_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Run-Stage([string]$name, [int]$iters, [int]$games, [int]$depth, [int]$verify) {
    $log = Join-Path $logDir ($name + ".log")
    Write-Host "==== [$name] iters=$iters games=$games depth=$depth verify=$verify ===="
    Write-Host "log => $log"
    & $trainer $iters $games $depth $verify *>&1 | Tee-Object -FilePath $log
}

# -------------------------------------------------------
# 16 小時分段策略（你電腦快慢不同，但這樣最「有效」）
#
# Phase 1: 快速探索（verify 小，找方向）
Run-Stage "01_explore_fast"  12000 25 2 120

# Phase 2: 穩定期（提高每次評估盤數，降低雜訊）
Run-Stage "02_stabilize"      6000 40 2 200

# Phase 3: 收斂期（提高深度到 3，開始真正在下棋）
Run-Stage "03_converge_d3"    2500 40 3 300

# Phase 4: 最終驗證（verify 拉高，確認不是噪音）
Run-Stage "04_final_verify"    800 80 3 800

Write-Host "`nALL DONE. Logs in: $logDir"
