[CmdletBinding()]
param(
    [string]$DeviceSerial = "emulator-5556",
    [ValidateSet("id1-start", "dopa-first", "mg1-first", "mg1-start", "mg3-first", "mg1-open", "mg3-open", "mg1-arena")]
    [string]$Recipe = "id1-start",
    [string]$LaunchArguments = "",
    [int]$WarmupSeconds = 20,
    [int]$CaptureSeconds = 60,
    [ValidateRange(0, 60)]
    [int]$RouteSettleSeconds = 2,
    [ValidateRange(1, 3600)]
    [int]$DemoPlaybackSeconds = 35,
    [switch]$Build,
    [switch]$Install,
    [switch]$RequireEmulator,
    [switch]$RequirePhysical,
    [string]$BaselinePath = "",
    [double]$MaxMeanAbsDiff = 0.0,
    [double]$MaxChangedFraction = 0.0,
    [string]$OutputRoot = "",
    [switch]$QualitySnapshot,
    [switch]$LifecycleSmoke
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path (Split-Path -Parent $repoRoot) "IronRift\Projects\Android"
$apk = Join-Path $androidRoot "build\outputs\apk\debug\ironrift-debug.apk"
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $repoRoot "build\android-gles-captures" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $OutputRoot "$Recipe-$stamp"
$recipeVersion = "phase0-v1"
$recipes = @{
    "id1-start" = @{ game = "id1"; map = "start"; label = "id1 control" }
    "dopa-first" = @{ game = "dopa"; map = "e5m1"; label = "DOPA first playable map" }
    "mg1-first" = @{ game = "mg1"; map = "mge1m1"; label = "MG1 first playable map" }
    "mg1-start" = @{ game = "mg1"; map = "start"; label = "MG1 start map" }
    "mg3-first" = @{ game = "mg3"; map = "map1"; label = "MG3 first playable map" }
    "mg1-open" = @{ game = "mg1"; map = "mge2m1"; label = "MG1 scripted demo route"; demo = "mg1_open_route" }
    "mg3-open" = @{ game = "mg3"; map = "start"; label = "MG3 scripted demo route"; demo = "mg3_open_route" }
    "mg1-arena" = @{ game = "mg1"; map = "start"; label = "MG1 arena battle demo"; demo = "mg1_arena_battle" }
}
$recipeInfo = $recipes[$Recipe]
if ($null -eq $recipeInfo) { throw "Unknown Phase 0 recipe: $Recipe" }
if ([string]::IsNullOrWhiteSpace($LaunchArguments)) {
    $LaunchArguments = "+game $($recipeInfo.game) +map $($recipeInfo.map)"; if ($recipeInfo.demo) { $LaunchArguments += " +playdemo $($recipeInfo.demo)" }; $LaunchArguments += " +r_speeds 3 +r_gles_perf_capture 1"
}
New-Item -ItemType Directory -Force $out | Out-Null

function Invoke-Adb([string[]]$AdbArgs) {
    & adb @AdbArgs
    if ($LASTEXITCODE -ne 0) { throw "adb failed ($LASTEXITCODE): adb $($AdbArgs -join ' ')" }
}
function Read-Adb([string[]]$AdbArgs) {
    $value = (& adb @AdbArgs | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "adb failed ($LASTEXITCODE): adb $($AdbArgs -join ' ')" }
    return $value
}

$release = Read-Adb @("-s", $DeviceSerial, "shell", "getprop", "ro.build.version.release")
$model = Read-Adb @("-s", $DeviceSerial, "shell", "getprop", "ro.product.model")
$renderer = Read-Adb @("-s", $DeviceSerial, "shell", "getprop", "ro.hardware.egl")
$qemu = Read-Adb @("-s", $DeviceSerial, "shell", "getprop", "ro.kernel.qemu")
$isEmulator = $qemu -eq "1" -or $qemu -eq "true" -or $DeviceSerial -like "emulator-*"
if ($RequireEmulator -and -not $isEmulator) { throw "Target is not identified as an Android emulator (ro.kernel.qemu='$qemu')." }
if ($RequirePhysical -and $isEmulator) { throw "Target is identified as an emulator, not a physical device." }
$targetClass = if ($DeviceSerial -like "emulator-*") { "android-emulator-host-gpu" } elseif ($isEmulator) { "android-emulator" } elseif ($RequirePhysical) { "physical-device" } else { "physical-or-virtual-device" }

if ($Build) {
    Push-Location $androidRoot
    try { & .\gradlew.bat assembleDebug; if ($LASTEXITCODE -ne 0) { throw "Android Debug build failed" } }
    finally { Pop-Location }
}
if ($Install) { Invoke-Adb @("-s", $DeviceSerial, "install", "-r", $apk) }
if ($recipeInfo.demo) {
    $demoPath = Join-Path $repoRoot "docs\$($recipeInfo.demo).dem"
    if (-not (Test-Path -LiteralPath $demoPath)) { throw "Demo file does not exist: $demoPath" }
    $demoDir = "/sdcard/ironwail/$($recipeInfo.game)"
    Invoke-Adb @("-s", $DeviceSerial, "shell", "mkdir", "-p", $demoDir)
    Invoke-Adb @("-s", $DeviceSerial, "push", $demoPath, "$demoDir/$($recipeInfo.demo).dem")
}

$routeStatus = "stationary-fixed-spawn"
$commandLine = "ironwail $LaunchArguments"
Invoke-Adb @("-s", $DeviceSerial, "shell", "mkdir -p /sdcard/ironwail")
Invoke-Adb @("-s", $DeviceSerial, "shell", "echo '$commandLine' > /sdcard/ironwail/commandline.txt")
Invoke-Adb @("-s", $DeviceSerial, "shell", "am", "force-stop", "com.ermac.ironwail")
Invoke-Adb @("-s", $DeviceSerial, "logcat", "-c")
Invoke-Adb @("-s", $DeviceSerial, "shell", "monkey", "-p", "com.ermac.ironwail", "1")
if ($LifecycleSmoke) {
    Start-Sleep -Seconds 2
    Invoke-Adb @("-s", $DeviceSerial, "shell", "input", "keyevent", "3")
    Start-Sleep -Seconds 2
    Invoke-Adb @("-s", $DeviceSerial, "shell", "monkey", "-p", "com.ermac.ironwail", "1")
    Start-Sleep -Seconds 5
}
if ($recipeInfo.demo) { $routeStatus = "demo-route:$($recipeInfo.demo)"; Start-Sleep -Seconds $DemoPlaybackSeconds } else { $routeStatus = "stationary-fixed-spawn" }
Start-Sleep -Seconds $RouteSettleSeconds
Start-Sleep -Seconds $WarmupSeconds
$start = Get-Date
Start-Sleep -Seconds $CaptureSeconds
$end = Get-Date

$engineLog = Read-Adb @("-s", $DeviceSerial, "shell", "cat", "/sdcard/ironwail/engine.log")
$logcat = Read-Adb @("-s", $DeviceSerial, "logcat", "-d", "-s", "IronRift:I", "Ironwail:I", "SDL:E", "*:S")
$gfx = Read-Adb @("-s", $DeviceSerial, "shell", "dumpsys", "gfxinfo", "com.ermac.ironwail")
$mem = Read-Adb @("-s", $DeviceSerial, "shell", "dumpsys", "meminfo", "com.ermac.ironwail")
$thermal = Read-Adb @("-s", $DeviceSerial, "shell", "dumpsys", "thermalservice")
$identity = @(
    "serial=$DeviceSerial"
    "target_class=$targetClass"
    "android_release=$release"
    "model=$model"
    "egl_hardware=$renderer"
    "ro.kernel.qemu=$qemu"
    "route_status=$routeStatus"
    "recipe_version=$recipeVersion"
    "game=$($recipeInfo.game)"
    "map=$($recipeInfo.map)"
    "recipe_label=$($recipeInfo.label)"
    "recipe=$Recipe"
    "launch_arguments=$LaunchArguments"
    "route_settle_seconds=$RouteSettleSeconds"
    "demo_playback_seconds=$DemoPlaybackSeconds"
    "demo_remote_path=/sdcard/ironwail/$($recipeInfo.game)/$($recipeInfo.demo).dem"
    "warmup_seconds=$WarmupSeconds"
    "capture_seconds=$CaptureSeconds"
    "lifecycle_smoke=$LifecycleSmoke"
    "capture_started=$start"
    "capture_finished=$end"
)
if ($QualitySnapshot) {
    function Get-LaunchCvar([string]$Name, [string]$Default) {
        $m = [regex]::Match($LaunchArguments, "(?:^|\s)\+$Name\s+([-+0-9.]+)")
        if ($m.Success) { return $m.Groups[1].Value }
        return $Default
    }
    $pss = "unknown"
    $pssMatch = [regex]::Match($mem, "(?mi)^\s*TOTAL\s+(\d+)")
    if ($pssMatch.Success) { $pss = $pssMatch.Groups[1].Value }
    $quality = @(
        "quality_snapshot=enabled"
        "render_scale=$(Get-LaunchCvar "r_scale" "default")"
        "msaa_samples=$(Get-LaunchCvar "vid_fsaa" "default-single-sample")"
        "anisotropy=$(Get-LaunchCvar "gl_texture_anisotropy" "default")"
        "dynamic_light_budget=$(Get-LaunchCvar "r_gles_dlight_max" "0")"
        "particle_mode=$(Get-LaunchCvar "r_particles" "default")"
        "water_warp_mode=$(Get-LaunchCvar "r_waterwarp" "default")"
        "postprocess_state=runtime-derived"
        "vsync=$(Get-LaunchCvar "vid_vsync" "default")"
        "fps_cap=$(Get-LaunchCvar "host_maxfps" "default")"
        "texture_memory=recorded-in-perf-samples"
        "process_memory_pss_kb=$pss"
    )
    $quality | Set-Content (Join-Path $out "quality-snapshot.txt")
    $identity += $quality
} else {
    "quality_snapshot=disabled" | Set-Content (Join-Path $out "quality-snapshot.txt")
}$identity | Set-Content (Join-Path $out "identity.txt")
$engineLog | Set-Content (Join-Path $out "engine.log")
$logcat | Set-Content (Join-Path $out "logcat.txt")
$gfx | Set-Content (Join-Path $out "gfxinfo.txt")
$mem | Set-Content (Join-Path $out "meminfo.txt")
$thermal | Set-Content (Join-Path $out "thermalinfo.txt")
$shot = Join-Path $out "screenshot.png"
Invoke-Adb @("-s", $DeviceSerial, "shell", "screencap", "-p", "/sdcard/ironwail/capture.png")
Invoke-Adb @("-s", $DeviceSerial, "pull", "/sdcard/ironwail/capture.png", $shot)
Invoke-Adb @("-s", $DeviceSerial, "shell", "am", "force-stop", "com.ermac.ironwail")
Add-Type -AssemblyName System.Drawing
function Get-ScreenshotMetrics([string]$Path) {
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $sum = [double]0
        $pixels = $bitmap.Width * $bitmap.Height
        for ($y = 0; $y -lt $bitmap.Height; $y++) {
            for ($x = 0; $x -lt $bitmap.Width; $x++) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sum += $pixel.R + $pixel.G + $pixel.B
            }
        }
        [pscustomobject]@{ Width = $bitmap.Width; Height = $bitmap.Height; MeanRgb = $sum / ($pixels * 3) }
    }
    finally { $bitmap.Dispose() }
}

$metrics = Get-ScreenshotMetrics $shot
"width=$($metrics.Width)`nheight=$($metrics.Height)`nmean_rgb=$($metrics.MeanRgb)" | Set-Content (Join-Path $out "screenshot-metrics.txt")
if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    if (-not (Test-Path -LiteralPath $BaselinePath)) { throw "Screenshot baseline does not exist: $BaselinePath" }
    $baseline = [System.Drawing.Bitmap]::new($BaselinePath)
    $current = [System.Drawing.Bitmap]::new($shot)
    try {
        if ($baseline.Width -ne $current.Width -or $baseline.Height -ne $current.Height) { throw "Screenshot dimensions differ: baseline=$($baseline.Width)x$($baseline.Height), current=$($current.Width)x$($current.Height)" }
        $sum = [double]0
        $changed = 0
        $pixels = $current.Width * $current.Height
        for ($y = 0; $y -lt $current.Height; $y++) {
            for ($x = 0; $x -lt $current.Width; $x++) {
                $a = $baseline.GetPixel($x, $y)
                $b = $current.GetPixel($x, $y)
                $delta = [Math]::Abs($a.R - $b.R) + [Math]::Abs($a.G - $b.G) + [Math]::Abs($a.B - $b.B)
                $sum += $delta / 3
                if ($delta -gt 24) { $changed++ }
            }
        }
        $meanAbsDiff = $sum / $pixels
        $changedFraction = $changed / $pixels
        "baseline=$BaselinePath`nmean_abs_diff=$meanAbsDiff`nchanged_fraction=$changedFraction`nmax_mean_abs_diff=$MaxMeanAbsDiff`nmax_changed_fraction=$MaxChangedFraction" | Set-Content (Join-Path $out "screenshot-diff.txt")
        if ($meanAbsDiff -gt $MaxMeanAbsDiff -or $changedFraction -gt $MaxChangedFraction) { throw "Screenshot regression exceeded thresholds: mean_abs_diff=$meanAbsDiff changed_fraction=$changedFraction" }
    }
    finally { $baseline.Dispose(); $current.Dispose() }
}

function Get-PerfPercentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $rank = [Math]::Ceiling(($Percentile / 100.0) * $sorted.Count) - 1
    $rank = [Math]::Max(0, [Math]::Min($rank, $sorted.Count - 1))
    return [double]$sorted[$rank]
}
function Get-PerfValue([string]$Line, [string]$Name) {
    $m = [regex]::Match($Line, "(?:^|\s)$Name=([-+0-9.]+)")
    if ($m.Success) { return [double]$m.Groups[1].Value }
    return $null
}
$perfLines = @($engineLog -split "`r?`n" | Where-Object { $_ -match "GLES perf:" })
$speedLines = @($engineLog -split "`r?`n" | Where-Object { $_ -match "^\s*[0-9]+\s+ms\s+" })
$speedMsValues = @(); $fpsValues = @()
foreach ($line in $speedLines) {
    $m = [regex]::Match($line, "^\s*([0-9]+)\s+ms\s+")
    if ($m.Success) {
        $frameMs = [double]$m.Groups[1].Value
        if ($frameMs -gt 0) { $speedMsValues += $frameMs; $fpsValues += (1000.0 / $frameMs) }
    }
}
$cpuValues = @(); $gpuValues = @(); $drawValues = @(); $stallValues = @(); $gpuValid = 0
foreach ($line in $perfLines) {
    $cpu = Get-PerfValue $line "cpu"; if ($null -ne $cpu) { $cpuValues += $cpu }
    $gpu = Get-PerfValue $line "gpu"; $valid = Get-PerfValue $line "valid"
    if ($null -ne $gpu -and $valid -eq 1) { $gpuValues += $gpu; $gpuValid++ }
    $draw = Get-PerfValue $line "draws"; if ($null -ne $draw) { $drawValues += $draw }
    $stall = Get-PerfValue $line "stalls"; if ($null -ne $stall) { $stallValues += $stall }
}
$summary = @(
    "format=phase0-perf-summary-v1"
    "sample_count=$($perfLines.Count)"
    "r_speeds_samples=$($speedMsValues.Count)"
    "r_speeds_frame_median_ms=$(Get-PerfPercentile $speedMsValues 50)"
    "r_speeds_frame_p90_ms=$(Get-PerfPercentile $speedMsValues 90)"
    "r_speeds_fps_median=$(Get-PerfPercentile $fpsValues 50)"
    "r_speeds_fps_worst=$(Get-PerfPercentile $fpsValues 0)"
    "gpu_valid_samples=$gpuValid"
    "cpu_median_ms=$(Get-PerfPercentile $cpuValues 50)"
    "cpu_p90_ms=$(Get-PerfPercentile $cpuValues 90)"
    "cpu_p99_ms=$(Get-PerfPercentile $cpuValues 99)"
    "cpu_worst_ms=$(Get-PerfPercentile $cpuValues 100)"
    "gpu_median_ms=$(Get-PerfPercentile $gpuValues 50)"
    "gpu_p90_ms=$(Get-PerfPercentile $gpuValues 90)"
    "gpu_p99_ms=$(Get-PerfPercentile $gpuValues 99)"
    "gpu_worst_ms=$(Get-PerfPercentile $gpuValues 100)"
    "draws_median=$(Get-PerfPercentile $drawValues 50)"
    "draws_p90=$(Get-PerfPercentile $drawValues 90)"
    "stalls_median=$(Get-PerfPercentile $stallValues 50)"
)
$summary | Set-Content (Join-Path $out "perf-summary.txt")
$perfLines | Set-Content (Join-Path $out "perf-samples.txt")
$speedLines | Set-Content (Join-Path $out "r-speeds-samples.txt")
Write-Output "Capture written to $out"
