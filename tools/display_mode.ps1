# Read or change the refresh rate of the display the game uses, for the
# frame-pacing tests (tools\pacing_matrix.ps1).
#   tools\display_mode.ps1 get              the main display's mode
#   tools\display_mode.ps1 set -Hz 120      change its refresh rate (same resolution);
#                                           the mode before is saved to work\display_mode.json
#   tools\display_mode.ps1 restore          back to the saved mode
# The display is the primary one, chosen by name from EnumDisplayDevices and
# never a virtual display (a "Virtual Display Driver" output is skipped).
param([Parameter(Position = 0)][ValidateSet('get', 'set', 'restore')][string]$Cmd = 'get',
      [int]$Hz = 0)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$saved = Join-Path $root 'work\display_mode.json'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Disp {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
        public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels;
        public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2;
        public int dmPanningWidth, dmPanningHeight;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct DISPLAY_DEVICE {
        public int cb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString;
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool EnumDisplayDevices(string dev, int i, ref DISPLAY_DEVICE dd, int flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int ChangeDisplaySettingsEx(string dev, ref DEVMODE dm, IntPtr hwnd, int flags, IntPtr p);
    public static string Primary() {
        for (int i = 0; ; i++) {
            var dd = new DISPLAY_DEVICE(); dd.cb = Marshal.SizeOf(dd);
            if (!EnumDisplayDevices(null, i, ref dd, 0)) return null;
            bool active = (dd.StateFlags & 1) != 0, primary = (dd.StateFlags & 4) != 0;
            if (active && primary && dd.DeviceString.IndexOf("Virtual", StringComparison.OrdinalIgnoreCase) < 0)
                return dd.DeviceName;
        }
    }
    public static DEVMODE Current(string dev) {
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(dm);
        EnumDisplaySettings(dev, -1, ref dm);
        return dm;
    }
    public static int SetHz(string dev, int hz) {
        var dm = Current(dev);
        dm.dmDisplayFrequency = hz;
        dm.dmFields = 0x400000;                 // DM_DISPLAYFREQUENCY
        return ChangeDisplaySettingsEx(dev, ref dm, IntPtr.Zero, 0, IntPtr.Zero);
    }
}
'@

$dev = [Disp]::Primary()
if (-not $dev) { throw 'no primary display found' }
$cur = [Disp]::Current($dev)
switch ($Cmd) {
    'get' {
        "$dev $($cur.dmPelsWidth)x$($cur.dmPelsHeight) @ $($cur.dmDisplayFrequency) Hz"
    }
    'set' {
        if ($Hz -le 0) { throw '-Hz is needed' }
        if (-not (Test-Path $saved)) {
            New-Item -ItemType Directory -Force -Path (Split-Path $saved) | Out-Null
            @{ device = $dev; hz = $cur.dmDisplayFrequency } | ConvertTo-Json | Set-Content $saved
        }
        $r = [Disp]::SetHz($dev, $Hz)
        if ($r -ne 0) { throw "ChangeDisplaySettingsEx returned $r ($Hz Hz not offered?)" }
        "$dev now @ $([Disp]::Current($dev).dmDisplayFrequency) Hz (was $($cur.dmDisplayFrequency))"
    }
    'restore' {
        if (-not (Test-Path $saved)) { "nothing saved; $dev stays @ $($cur.dmDisplayFrequency) Hz"; return }
        $s = Get-Content $saved | ConvertFrom-Json
        $r = [Disp]::SetHz($s.device, [int]$s.hz)
        if ($r -ne 0) { throw "ChangeDisplaySettingsEx returned $r" }
        Remove-Item $saved
        "$($s.device) restored @ $([Disp]::Current($s.device).dmDisplayFrequency) Hz"
    }
}
