<#
.SYNOPSIS
Installs FlareSimPS.8bf, its lens assets and the CUDA runtime into Photoshop.

.DESCRIPTION
Copies into "<Photoshop>\Plug-ins\FlareSim\":
  FlareSimPS.8bf, cudart64_12.dll (when found) and assets\lenses.
Copies "FlareSim Preset.jsx" and flaresim.jsxinc into "<Photoshop>\Presets\Scripts\".
Photoshop must be restarted to pick up a new or updated plug-in.
Relaunches itself elevated when the Plug-ins folder is not writable.

.EXAMPLE
pwsh scripts/install_ps_plugin.ps1
pwsh scripts/install_ps_plugin.ps1 -Config Debug -PhotoshopDir "C:\Program Files\Adobe\Adobe Photoshop 2025"
#>
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build-ps"),
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$PhotoshopDir = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not $PhotoshopDir) {
    $candidate = Get-ChildItem "C:\Program Files\Adobe" -Directory -Filter "Adobe Photoshop 20*" -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "Photoshop.exe") } |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $candidate) { throw "No Photoshop installation found under C:\Program Files\Adobe. Pass -PhotoshopDir." }
    $PhotoshopDir = $candidate.FullName
}

$plugin = Join-Path (Resolve-Path $BuildDir).Path "src\ps\$Config\FlareSimPS.8bf"
if (-not (Test-Path $plugin)) { throw "Plug-in not built: $plugin (cmake --build build-ps --config $Config --target flaresim_ps_plugin)" }

$target = Join-Path $PhotoshopDir "Plug-ins\FlareSim"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $probe = Join-Path $PhotoshopDir "Plug-ins\.flaresim_write_probe"
    $writable = $true
    try { [IO.File]::WriteAllText($probe, ""); Remove-Item $probe } catch { $writable = $false }
    if (-not $writable) {
        Write-Host "Plug-ins folder needs administrator rights; requesting elevation..."
        $shell = (Get-Process -Id $PID).Path
        $elevatedArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"",
                  "-BuildDir", "`"$((Resolve-Path $BuildDir).Path)`"", "-Config", $Config, "-PhotoshopDir", "`"$PhotoshopDir`"")
        $proc = Start-Process -FilePath $shell -ArgumentList $elevatedArgs -Verb RunAs -Wait -PassThru
        exit $proc.ExitCode
    }
}

if (Get-Process Photoshop -ErrorAction SilentlyContinue) {
    Write-Warning "Photoshop is running. The .8bf may be locked; restart Photoshop after installing."
}

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item $plugin $target -Force

$cudart = $null
$cudaRoots = @($env:CUDA_PATH) + (Get-ChildItem "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA" -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | ForEach-Object { $_.FullName })
foreach ($root in $cudaRoots) {
    if ($root -and (Test-Path (Join-Path $root "bin\cudart64_12.dll"))) { $cudart = Join-Path $root "bin\cudart64_12.dll"; break }
}
if ($cudart) {
    Copy-Item $cudart $target -Force
} else {
    Write-Warning "cudart64_12.dll not found; the plug-in only loads if it is on PATH."
}

$assets = Join-Path $target "assets"
if (Test-Path $assets) { Remove-Item $assets -Recurse -Force }
New-Item -ItemType Directory -Force $assets | Out-Null
Copy-Item (Join-Path $repo "assets\lenses") $assets -Recurse -Force

# File > Scripts > "FlareSim Preset..." and the helper library it includes.
$scripts = Join-Path $PhotoshopDir "Presets\Scripts"
if (Test-Path $scripts) {
    Copy-Item (Join-Path $repo "scripts\photoshop\FlareSim Preset.jsx") $scripts -Force
    Copy-Item (Join-Path $repo "scripts\photoshop\flaresim.jsxinc") $scripts -Force
} else {
    Write-Warning "$scripts not found; skipped the FlareSim Preset script."
}

Write-Host "Installed FlareSim to $target"
Get-ChildItem $target | Select-Object Name, Length | Format-Table -AutoSize
exit 0
