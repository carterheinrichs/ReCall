[CmdletBinding()]
param([string]$Executable = "$PSScriptRoot/../out/build/Release/ReCall.exe")
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ReCallSmoke {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    public static string Diagnostics(uint pid) {
        string result = "";
        EnumWindows((h,l) => { uint owner; GetWindowThreadProcessId(h, out owner);
            if(owner == pid) { var s = new System.Text.StringBuilder(1024); GetWindowText(h,s,1024); result += s.ToString() + " | ";
                EnumChildWindows(h,(c,x) => { var t = new System.Text.StringBuilder(2048); GetWindowText(c,t,2048); result += t.ToString() + " | "; return true; },IntPtr.Zero); }
            return true; },IntPtr.Zero);
        return result;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr h, IntPtr after, string cls, string name);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, string text);
}
'@
if ([ReCallSmoke]::FindWindow('ReCall.Palette','ReCall') -ne [IntPtr]::Zero) { throw 'Exit the existing ReCall instance before smoke testing.' }
$process = Start-Process -FilePath $Executable -PassThru
$hwnd = [IntPtr]::Zero
$phase = 'startup'
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ($hwnd -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) { throw "ReCall exited during startup: $($process.ExitCode)" }
        Start-Sleep -Milliseconds 50
        $hwnd = [ReCallSmoke]::FindWindow('ReCall.Palette','ReCall')
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'Palette window was not created' }
    $phase = 'hidden startup'
    Start-Sleep -Milliseconds 300
    if ([ReCallSmoke]::IsWindowVisible($hwnd)) { throw 'Palette must start hidden' }
    $edit = [ReCallSmoke]::FindWindowEx($hwnd,[IntPtr]::Zero,'EDIT',$null)
    if ($edit -eq [IntPtr]::Zero) { throw 'Search control missing' }
    $phase = 'show'
    [void][ReCallSmoke]::PostMessage($hwnd,0x8009,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    if (![ReCallSmoke]::IsWindowVisible($hwnd)) { throw 'Palette failed to show' }
    $phase = 'query'
    [void][ReCallSmoke]::SendMessage($edit,0x000C,[IntPtr]::Zero,':smile')
    Start-Sleep -Milliseconds 700
    $phase = 'clipboard queries and route changes'
    foreach ($query in @('>', '> smoke-test', ':smile', '>')) {
        [void][ReCallSmoke]::SendMessage($edit,0x000C,[IntPtr]::Zero,$query)
        Start-Sleep -Milliseconds 100
    }
    $phase = 'theme notifications'
    [void][ReCallSmoke]::PostMessage($hwnd,0x001A,[IntPtr]::Zero,[IntPtr]::Zero)
    [void][ReCallSmoke]::PostMessage($hwnd,0x031A,[IntPtr]::Zero,[IntPtr]::Zero)
    [void][ReCallSmoke]::PostMessage($hwnd,0x0015,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    $phase = 'navigation and dismissal'
    [void][ReCallSmoke]::PostMessage($edit,0x0100,[IntPtr]0x28,[IntPtr]::Zero)
    [void][ReCallSmoke]::PostMessage($edit,0x0100,[IntPtr]0x1B,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    if ([ReCallSmoke]::IsWindowVisible($hwnd)) { throw 'Escape failed to hide the palette' }
    if ($process.HasExited) { throw 'Process exited while exercising the palette' }
    'Native smoke test passed: hidden startup, show, clipboard/emoji queries, theme notifications, navigation, Escape.'
} catch {
    Write-Warning "Smoke test failed during $phase : $_"
    Write-Warning ([ReCallSmoke]::Diagnostics($process.Id))
    throw
} finally {
    if ($hwnd -ne [IntPtr]::Zero) { [void][ReCallSmoke]::PostMessage($hwnd,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) }
    if (!$process.WaitForExit(15000)) { $process.Kill(); throw 'ReCall did not shut down within 15 seconds' }
}
