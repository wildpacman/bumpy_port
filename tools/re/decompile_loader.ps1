# Lean one-pass Ghidra headless decompilation export via PyGhidra.
# Reuses the already-downloaded Ghidra 12.1.2 + bundled JDK + pyghidra venv.
# No hash gates, no dual import, no junction: the main checkout path has no
# dotted component, so Ghidra accepts the project location directly.
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$tools = Join-Path $root ".worktrees\resource-formats-and-menu\analysis\generated\ghidra-tools"
$ghidra = Join-Path $tools "ghidra_12.1.2_PUBLIC"
$jdk = Join-Path $tools "jdk-21.0.11+10"
$venvPython = Join-Path $tools "pyghidra-venv\Scripts\python.exe"
$launcher = Join-Path $ghidra "Ghidra\Features\PyGhidra\support\pyghidra_launcher.py"
$input = Join-Path $root "analysis\generated\BUMPY.UNPACKED.EXE"
$scripts = Join-Path $root "analysis\ghidra_scripts"
$projLoc = Join-Path $root "analysis\generated\ghidra-loader-proj"
$outDir = Join-Path $root "analysis\generated\decomp"
$log = Join-Path $root "analysis\generated\decompile_loader.log"

foreach ($p in @($venvPython, $launcher, (Join-Path $jdk "bin\java.exe"), $input)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "missing required path: $p" }
}
if (Test-Path -LiteralPath $projLoc) { Remove-Item -LiteralPath $projLoc -Recurse -Force }
New-Item -ItemType Directory -Force -Path $projLoc | Out-Null
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$env:JAVA_HOME = $jdk
Write-Host "Running Ghidra (PyGhidra) headless: import + analyze + decompile export..."
& $venvPython $launcher $ghidra -H $projLoc "BumpyLoader" `
    -import $input `
    -loader MzLoader `
    -processor "x86:LE:16:Real Mode" `
    -scriptPath $scripts `
    -postScript ExportLoaderDecomp.py $outDir `
    -analysisTimeoutPerFile 900 `
    -log $log
if ($LASTEXITCODE -ne 0) { throw "pyghidra headless failed with exit $LASTEXITCODE" }
Write-Host "Done. Decompilation exported to: $outDir"
Get-ChildItem -LiteralPath $outDir | Select-Object Name, Length
