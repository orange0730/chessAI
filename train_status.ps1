# Returns a single-line JSON status for D:\code\chess_train
$ErrorActionPreference = 'SilentlyContinue'
Set-Location 'D:\code\chess_train'

$p = Get-Process -Name 'trainer' -ErrorAction SilentlyContinue
$trainerRunning = $null -ne $p
$trainerPid = if ($trainerRunning) { [int]$p.Id } else { $null }

$logsDir = Get-ChildItem -Directory -Filter 'logs_*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$logsDirPath = if ($logsDir) { $logsDir.FullName } else { $null }
$logsDirMtime = if ($logsDir) { $logsDir.LastWriteTime.ToString('o') } else { $null }

$latestLog = $null
if ($logsDir) {
  $latestLog = Get-ChildItem -File -Path $logsDir.FullName | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
$latestLogPath = if ($latestLog) { $latestLog.FullName } else { $null }
$latestLogMtime = if ($latestLog) { $latestLog.LastWriteTime.ToString('o') } else { $null }

function FileInfo($name) {
  if (Test-Path $name) {
    $i = Get-Item $name
    return @{ exists=$true; mtime=$i.LastWriteTime.ToString('o'); bytes=[int64]$i.Length }
  }
  return @{ exists=$false; mtime=$null; bytes=$null }
}

$checkpoint = FileInfo 'checkpoint.bin'
$weights    = FileInfo 'weights.txt'

# Try to extract an iter number from the tail of latest log (best-effort)
$iter = $null
$tail = $null
if ($latestLog) {
  $tail = Get-Content $latestLog.FullName -Tail 40
  $m = ($tail | Select-String -Pattern 'iter\s+(\d+)' | Select-Object -Last 1)
  if ($m -and $m.Matches.Count -gt 0) { $iter = [int]$m.Matches[0].Groups[1].Value }
}

$obj = [ordered]@{
  at = (Get-Date).ToString('o')
  root = (Get-Location).Path
  trainer = @{ running=$trainerRunning; pid=$trainerPid }
  logs = @{ dir=$logsDirPath; dirMtime=$logsDirMtime; latest=$latestLogPath; latestMtime=$latestLogMtime; iter=$iter }
  files = @{ checkpoint=$checkpoint; weights=$weights }
}

($obj | ConvertTo-Json -Compress)
