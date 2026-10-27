# Windows twin of scripts/regen.sh: regenerate everything derived from the XBE
# -- analysis, the per-function asm split (decomp\) and the lifted C
# (src\recomp\gen\). Neither output is in git (both are the game's code), so
# run this after cloning and whenever an override is added to
# src\ssx3_overrides.c (it excludes those bodies).
#
#   powershell -ExecutionPolicy Bypass -File scripts\regen.ps1          # all steps (~2-4 min)
#   powershell -ExecutionPolicy Bypass -File scripts\regen.ps1 lift     # only the C lift
#
# XBE: $env:XBE, else game\default.xbe, else ..\SSX3\default.xbe. The analysis
# JSON is written beside it.
param([string]$Step = "all")
# Native tools write progress to stderr, which "Stop" would turn into a
# fatal error under Windows PowerShell 5.1: exit codes are checked instead.
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $PSScriptRoot
$tk = Join-Path (Split-Path -Parent $here) "xboxrecomp"
$py = Join-Path $tk ".venv\Scripts\python.exe"
if (-not (Test-Path $py)) { throw "no toolkit venv: run scripts\setup.ps1 first" }
$xbe = $env:XBE
if (-not $xbe) {
    foreach ($c in @((Join-Path $here "game\default.xbe"), (Join-Path (Split-Path -Parent $here) "SSX3\default.xbe"))) {
        if (Test-Path $c) { $xbe = $c; break }
    }
}
if (-not $xbe -or -not (Test-Path $xbe)) { throw "default.xbe not found (put the rip in game\ or set XBE)" }
$xbe = (Resolve-Path $xbe).Path
$analysis = Join-Path (Split-Path -Parent $xbe) "default_analysis.json"
# DOLBY is DSP56300 microcode for the audio chip, not x86: never disassemble it.
$sections = @("--text-only", "--extra-sections", "D3D,D3DX,XGRPH,DSOUND,XPP")
function Py { & $py @args; if ($LASTEXITCODE) { throw "python $($args[0..1] -join ' ') failed" } }

# Which build the XBE is (tools\xbe_port\port.py): the hand-written code uses
# PAL addresses, and a port file maps them to another build's. This writes
# port_map.h, the seeds and the override file to scan, in the build's addresses.
$port = Join-Path $here "work\port"
$variant = (& $py -I (Join-Path $here "tools\xbe_port\port.py") $xbe $here $port)
if ($LASTEXITCODE) { throw "tools\xbe_port\port.py failed" }
"build: $variant"
# Entry points only reached through function pointers, measured at runtime.
$seeds = @("--seed-functions", (Join-Path $port "icall_seeds.json"))

Push-Location $tk
try {
    if ($Step -eq "all") {
        Py -m tools.xbe_parser $xbe --json $analysis | Out-Null
        Py -m tools.disasm $xbe @sections @seeds
        Py -m tools.func_id $xbe
        Py -m tools.abi_analysis $xbe
        # 143 FidDb names from Ghidra (analysis\ghidra_names.json), applied
        # without Ghidra; must follow tools.disasm, which rewrites functions.json.
        # The names are at PAL addresses: on another build they would land on the wrong functions.
        if ($variant -eq "pal") {
            Py tools\ghidra_naming\merge_names.py --names-json (Join-Path $here "analysis\ghidra_names.json") --apply
        }
        Py (Join-Path $here "tools\split_named.py") --xboxrecomp $tk $xbe --out (Join-Path $here "decomp") @sections @seeds
    }
    Py -m tools.recomp $xbe --all --split 250 --game-name "SSX 3" `
        --gen-dir (Join-Path $here "src\recomp\gen") `
        --exclude-manual (Join-Path $port "ssx3_overrides.c")
    Copy-Item (Join-Path $port "port_map.h") (Join-Path $here "src\recomp\gen\port_map.h")
} finally { Pop-Location }
"regen done ($Step)"
