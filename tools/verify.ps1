$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [scriptblock] $Command,
        [Parameter(Mandatory)]
        [string] $Description
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

Push-Location $root
try {
    Invoke-Checked { python tools/assets/manifest.py verify } "Asset verification"
    Invoke-Checked { python -m unittest discover -s tests/python -v } "Python tests"
    Invoke-Checked { cmake --preset windows-debug } "CMake configure"
    Invoke-Checked { cmake --build --preset windows-debug } "CMake build"
    Invoke-Checked { ctest --preset windows-debug } "C++ tests"

    $functions = @(Import-Csv analysis/catalog/functions.csv)
    if ($functions.Count -eq 0) {
        throw "Ghidra function catalog is empty"
    }
} finally {
    Pop-Location
}
