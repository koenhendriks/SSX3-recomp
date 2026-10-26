# Frame pacing across refresh rates: for each rate, set the main display to
# it (tools\display_mode.ps1), run a race with RECOMP_FRAME_STATS, bring the
# game to the front (G-Sync applies to the foreground window only) and sum
# up the [GLASS] lines: what reached the display. The display mode is put
# back afterwards, whatever happens. Writes logs\pacing_matrix_<time>.md.
#   tools\pacing_matrix.ps1 [-Rates 60,120,144] [-Seconds 45] [-Fullscreen]
# Variable refresh is the driver's setting and cannot be switched from here:
# run once with G-Sync/FreeSync on and once with it off when both matter.
param([int[]]$Rates = @(60, 120, 144), [int]$Seconds = 45, [switch]$Fullscreen)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$mode = Join-Path $PSScriptRoot 'display_mode.ps1'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $root "logs\pacing_matrix_$stamp.md"
$rows = @()

function Summarise([string]$log) {
    $g = Select-String -Path $log -Pattern '\[GLASS\] (.+?) ([\d.]+) Hz (\S+) \| shown (\d+), on-glass interval ([\d.]+)-([\d.]+) ms, over 25 ms (\d+) \| presents ([\d.]+)-([\d.]+) ms.*past its aim (\d+), lead (\d+) ms'
    $race = $g | Select-Object -Last ([Math]::Min($g.Count, $Seconds - 5))
    if (-not $race) { return $null }
    $m = @($race | ForEach-Object { ,$_.Matches[0].Groups })
    [pscustomobject]@{
        strategy = ($m | ForEach-Object { $_[1].Value } | Group-Object | Sort-Object Count -Descending | Select-Object -First 1).Name
        vrr      = ($m | ForEach-Object { $_[3].Value } | Group-Object | Sort-Object Count -Descending | Select-Object -First 1).Name
        seconds  = $race.Count
        glassMin = ($m | ForEach-Object { [double]$_[5].Value } | Measure-Object -Minimum).Minimum
        glassMax = ($m | ForEach-Object { [double]$_[6].Value } | Measure-Object -Maximum).Maximum
        evenSecs = @($m | Where-Object { [double]$_[6].Value - [double]$_[5].Value -le 1.0 }).Count
        long     = ($m | ForEach-Object { [int]$_[7].Value } | Measure-Object -Sum).Sum
        presMin  = ($m | ForEach-Object { [double]$_[8].Value } | Measure-Object -Minimum).Minimum
        presMax  = ($m | ForEach-Object { [double]$_[9].Value } | Measure-Object -Maximum).Maximum
        late     = ($m | ForEach-Object { [int]$_[10].Value } | Measure-Object -Sum).Sum
    }
}

try {
    foreach ($hz in $Rates) {
        & $mode set -Hz $hz | Write-Host
        Start-Sleep 2
        $env = @{ RECOMP_RES = '1440'; RECOMP_WIDESCREEN = 'auto'; RECOMP_FRAME_STATS = '1' }
        if ($Fullscreen) { $env.RECOMP_FULLSCREEN = '1' }
        $front = Start-Job -ScriptBlock { param($p) Start-Sleep 8; & $p -WaitSec 40 } -ArgumentList (Join-Path $PSScriptRoot 'front.ps1')
        & (Join-Path $PSScriptRoot 'bench.ps1') -Seconds $Seconds -Label "matrix$hz" -Env $env | Select-Object -Last 1 | Write-Host
        $front | Wait-Job | Remove-Job
        $s = Summarise (Join-Path $root 'stderr.txt')
        $rows += [pscustomobject]@{ hz = $hz; s = $s }
    }
} finally {
    & $mode restore | Write-Host
}

$lines = @("# Frame pacing matrix, $stamp", "",
           "Main display, race at 1440p, $Seconds s each$(if ($Fullscreen) { ', fullscreen' }). On-glass intervals from DXGI frame statistics.", "",
           "| Display | Strategy | Refresh | On-glass interval (ms) | Seconds with a spread <= 1 ms | Frames over 25 ms | Present spacing (ms) | Frames past their aim |",
           "|---|---|---|---|---|---|---|---|")
foreach ($r in $rows) {
    if (-not $r.s) { $lines += "| $($r.hz) Hz | (no [GLASS] lines) | | | | | | |"; continue }
    $s = $r.s
    $lines += "| $($r.hz) Hz | $($s.strategy) | $($s.vrr) | $($s.glassMin)-$($s.glassMax) | $($s.evenSecs) of $($s.seconds) | $($s.long) | $($s.presMin)-$($s.presMax) | $($s.late) |"
}
$lines | Set-Content $out
$lines | Write-Host
"written: $out"
