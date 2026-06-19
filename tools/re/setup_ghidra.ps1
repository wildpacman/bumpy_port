# Lean Ghidra setup: extract the pinned Ghidra + JDK archives and create a
# PyGhidra virtual environment under ignored analysis/generated/ghidra-tools.
# Idempotent; re-run only recreates what is missing. No hash gates, no dual
# import. The decompilation runner (decompile_loader.ps1) depends on this layout.
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$downloads = Join-Path $root "tools\vendor\downloads"
$toolRoot = Join-Path $root "analysis\generated\ghidra-tools"
$ghidraZip = Join-Path $downloads "ghidra_12.1.2_PUBLIC_20260605.zip"
$jdkZip = Join-Path $downloads "OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip"

foreach ($zip in @($ghidraZip, $jdkZip)) {
    if (-not (Test-Path -LiteralPath $zip)) {
        throw "missing pinned archive: $zip  (download per analysis/README.md into tools/vendor/downloads)"
    }
}

New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
$ghidra = Join-Path $toolRoot "ghidra_12.1.2_PUBLIC"
$jdk = Join-Path $toolRoot "jdk-21.0.11+10"
if (-not (Test-Path -LiteralPath $ghidra)) { Expand-Archive -LiteralPath $ghidraZip -DestinationPath $toolRoot }
if (-not (Test-Path -LiteralPath $jdk)) { Expand-Archive -LiteralPath $jdkZip -DestinationPath $toolRoot }

$venv = Join-Path $toolRoot "pyghidra-venv"
$venvPython = Join-Path $venv "Scripts\python.exe"
$wheelDir = Join-Path $ghidra "Ghidra\Features\PyGhidra\pypkg\dist"
if (-not (Test-Path -LiteralPath $venvPython)) {
    & py -3.12 -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw "could not create venv (need 'py -3.12')" }
    & $venvPython -m pip install --disable-pip-version-check --no-index --find-links $wheelDir pyghidra
    if ($LASTEXITCODE -ne 0) { throw "could not install pyghidra into the venv" }
}

Write-Host "Ghidra tools ready: $toolRoot"
