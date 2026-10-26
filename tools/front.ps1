# Bring the running game's window to the front (for a person watching a
# scripted run). Windows does not let a background process take the
# foreground; a synthetic Alt press first is the usual way past that lock.
#   tools\front.ps1 [-WaitSec 30]    waits up to -WaitSec for the window
param([int]$WaitSec = 30)
Add-Type -Name F -Namespace U -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int c);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
'@
$end = (Get-Date).AddSeconds($WaitSec)
do {
    $p = Get-Process ssx3_recomp -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { break }
    Start-Sleep -Milliseconds 250
} while ((Get-Date) -lt $end)
if (-not $p) { 'no game window'; exit 1 }
[U.F]::keybd_event(0x12, 0, 0, [System.UIntPtr]::Zero)          # Alt down
[U.F]::keybd_event(0x12, 0, 2, [System.UIntPtr]::Zero)          # Alt up
[U.F]::ShowWindow($p.MainWindowHandle, 9) | Out-Null              # SW_RESTORE
if ([U.F]::SetForegroundWindow($p.MainWindowHandle)) { 'game window in front' } else { 'could not bring the game window to the front' }
