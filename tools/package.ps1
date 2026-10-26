# Release folder + zip for players: launcher.exe, ssx3_recomp.exe, LICENSE,
# keys.example.ini, a short README and hd-textures\ (Bl4ckH4nd's HD texture
# pack with its map, ~370 MB) -- no game files (the player points the
# launcher at their own SSX 3 rip). Build first (tools\build.bat).
#   powershell -File tools\package.ps1 [-Out dist] [-Name SSX3-recomp] [-Diag]
# -Diag adds play_diag.bat (the launcher with frame-timing diagnostics on),
# and ssx3_recomp.pdb when the build has one (docs\PACING.md).
param([string]$Out = "dist", [string]$Name = "SSX3-recomp", [switch]$Diag)
$root = Split-Path -Parent $PSScriptRoot
$dir = Join-Path (Join-Path $root $Out) $Name
foreach ($f in @("build\launcher.exe", "build\ssx3_recomp.exe")) {
    if (-not (Test-Path (Join-Path $root $f))) { throw "$f missing: run tools\build.bat first" }
}
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Copy-Item (Join-Path $root "build\launcher.exe") $dir
Copy-Item (Join-Path $root "build\ssx3_recomp.exe") $dir
Copy-Item (Join-Path $root "keys.example.ini") $dir
Copy-Item (Join-Path $root "LICENSE") (Join-Path $dir "LICENSE.txt")
Copy-Item (Join-Path $root "docs\PLAYER-README.txt") (Join-Path $dir "README.txt")
$hd = Join-Path $dir "hd-textures"
New-Item -ItemType Directory -Force -Path $hd | Out-Null
Copy-Item (Join-Path $root "hd-textures\*") $hd -Include "*.dds", "ssx3_map.txt", "README.txt"
if ($Diag) {
    $pdb = Join-Path $root "build\ssx3_recomp.pdb"
    if (Test-Path $pdb) { Copy-Item $pdb $dir }   # only in a build with debug info
    @(
        '@echo off',
        'rem The launcher with frame-timing diagnostics on: the game''s log (Open logs)',
        'rem then records frame times, what reached the display, and what every',
        'rem thread was doing in any frame over 25 ms. Send the logs folder as a zip.',
        'set RECOMP_FRAME_STATS=1',
        'set RECOMP_SLOW_STACKS=25',
        'set RECOMP_PERF_HITCH_MS=25',
        'set RECOMP_GUESTCPU_STATS=1',
        'start "" "%~dp0launcher.exe"'
    ) | Set-Content -Encoding ascii (Join-Path $dir "play_diag.bat")
}
$zip = Join-Path (Join-Path $root $Out) "$Name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path $dir -DestinationPath $zip
"$dir"
"$zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
