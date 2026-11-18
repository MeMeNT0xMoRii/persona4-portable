[CmdletBinding()]
param(
    [ValidateSet('OpenGL','Software')][string]$Renderer = 'OpenGL',
    [string]$Route,
    [ValidateSet('P4','P4-perf03','P4-perf04','P4-perf04-timing')][string]$GameFolder = 'P4',
    [ValidateRange(5,900)][int]$TimeoutSeconds = 120,
    [Alias("CaptureJunes")][switch]$CaptureScene,
    [string]$SceneMarker = "f009_004_cart filter mode",
    [ValidateRange(0,30000)][int]$CaptureDelayMilliseconds = 4000,
    [ValidateRange(1,12)][int]$ScreenshotCount = 1,
    [switch]$Manual,
    [string]$LabRoot = (Join-Path $PSScriptRoot '../build/ppsspp-lab')
)
$ErrorActionPreference = 'Stop'
$LabRoot = (Resolve-Path -LiteralPath $LabRoot).Path
if ($Manual -and $CaptureScene) { throw 'Choose manual control or automatic capture.' }
$emulator = Join-Path $LabRoot 'emulator'
$exe = Join-Path $emulator 'PPSSPPWindows64.exe'
$psp = Join-Path $emulator 'memstick\PSP'
$game = Join-Path $psp (Join-Path "GAME\CAT_Ports" $GameFolder)
if (Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue | Where-Object Path -EQ $exe) {
    throw 'Close the isolated PPSSPP window before starting another run.'
}
$timingRun = $GameFolder -eq 'P4-perf04-timing'
$logName = if ($timingRun) { 'p4-timing.log' } else { 'p4.log' }
$inputs = @()
if ($Route) {
    $inputs = @(Get-Content -LiteralPath $Route)
    if ($inputs.Count -gt 1024 -or ($inputs -join [char]10).Length -ge 16383 -or
        ($inputs | Where-Object { $_ -notmatch '^(S \d+(\.\d+)? [0-9a-fA-F]{4}|A \d+(\.\d+)? \d{1,3} \d{1,3})$' })) {
        throw 'The route does not fit the perf02 PADS/STICK input format.'
    }
}

$run = Join-Path $LabRoot ('results\{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $Renderer)
$null = New-Item -ItemType Directory -Path $run
foreach ($name in $logName,'route.txt') {
    $path = Join-Path $game $name
    if (Test-Path -LiteralPath $path) {
        Move-Item -LiteralPath $path -Destination (Join-Path $run "previous-$name")
    }
}
if ($inputs.Count) {
    [IO.File]::WriteAllText("$game\route.txt", ($inputs -join [char]10) + [char]10)
    Copy-Item -LiteralPath "$game\route.txt" -Destination $run
}
$config = Get-Content -LiteralPath "$LabRoot\backup\config\ppsspp.ini" -Raw
$software = ($Renderer -eq 'Software').ToString()
$config = $config -replace '(?m)^SoftwareRenderer = [^\r\n]+',"SoftwareRenderer = $software"
$config = $config -replace '(?m)^GraphicsBackend = [^\r\n]+','GraphicsBackend = 0 (OPENGL)'
$config = $config -replace '(?m)^ScreenshotsAsPNG = [^\r\n]+','ScreenshotsAsPNG = True'
$config = $config -replace '(?m)^FileName\d+ = [^\r\n]*\r?\n',''
Set-Content -LiteralPath "$psp\SYSTEM\ppsspp.ini" -Value $config -Encoding utf8
Copy-Item -LiteralPath "$psp\SYSTEM\ppsspp.ini" -Destination $run
Get-FileHash -LiteralPath "$game\EBOOT.PBP","$game\p4data.bin","$game\p4str.bin" |
    Export-Csv -LiteralPath "$run\runtime-sha256.csv" -NoTypeInformation
function Get-LabCapture {
    Get-ChildItem -LiteralPath "$psp\SCREENSHOT" -File -Filter '*.png' -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $psp -Recurse -File -Filter '*.ppdmp'
}
$captures = @(Get-LabCapture | ForEach-Object FullName)
Write-Host "Active run: $run"
if ($CaptureScene -and !( 'P4LabMenu' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class P4LabMenu {
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr menu, uint id, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint msg, IntPtr id, IntPtr value);
}
"@
}
$proc = Start-Process -FilePath $exe -WorkingDirectory $emulator -ArgumentList ('"{0}"' -f "$game\EBOOT.PBP") -PassThru
try {
    if ($Manual) { $proc.WaitForExit() }
    else {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $found = $false
        while (!$proc.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 500
            if ($CaptureScene -and (Test-Path -LiteralPath "$game\$logName") -and
                (Select-String -LiteralPath "$game\$logName" -SimpleMatch $SceneMarker -Quiet)) {
                if ($CaptureDelayMilliseconds) { Start-Sleep -Milliseconds $CaptureDelayMilliseconds }
                $proc.Refresh()
                $window = $proc.MainWindowHandle
                $commands = @(40066) * $ScreenshotCount + @(40062)
                foreach ($command in $commands) {
                    $state = [P4LabMenu]::GetMenuState([P4LabMenu]::GetMenu($window),$command,0)
                    if ($state -eq [uint32]::MaxValue -or ($state -band 3)) {
                        throw "PPSSPP menu command $command unavailable"
                    }
                    if (![P4LabMenu]::PostMessage($window,273,[IntPtr]$command,[IntPtr]::Zero)) { throw "PPSSPP capture request failed" }
                    if ($command -eq 40066 -and $ScreenshotCount -gt 1) { Start-Sleep -Milliseconds 277 }
                    else { Start-Sleep -Seconds 2 }
                }
                $found = $true
                break
            }
        }
        if ($CaptureScene -and !$found) { Write-Warning 'Scene capture checkpoint not reached' }
    }
} finally {
    if (!$proc.HasExited) {
        if (Test-Path -LiteralPath "$game\$logName") { Copy-Item -LiteralPath "$game\$logName" -Destination "$run\before-close.log" }
        $closed = $proc.CloseMainWindow() -and $proc.WaitForExit(15000)
        if (!$closed) { Write-Warning "Process $($proc.Id) did not close within 15 seconds." }
    }
}
if (Test-Path -LiteralPath "$game\$logName") {
    Copy-Item -LiteralPath "$game\$logName" -Destination $run
}
Get-LabCapture |
    Where-Object { $_.FullName -notin $captures } |
    Copy-Item -Destination $run

if (!$proc.HasExited) { throw "Lab process $($proc.Id) still needs to close." }
if ($CaptureScene -and (!$found -or
    @(Get-ChildItem -LiteralPath $run -Filter '*.png').Count -lt $ScreenshotCount -or
    !(Get-ChildItem -LiteralPath $run -Filter '*.ppdmp'))) {
    throw "Capture incomplete. Inspect the archived run in $run"
}
