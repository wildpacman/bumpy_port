$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vendorPath = Join-Path $root "tools\vendor"
New-Item -ItemType Directory -Force -Path $vendorPath | Out-Null
$vendor = (Resolve-Path $vendorPath).Path

foreach ($tool in @("python", "cmake", "git", "cl")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool is not on PATH: $tool"
    }
}

function Invoke-Git {
    param(
        [Parameter(Mandatory)]
        [string[]] $GitArguments
    )

    & git @GitArguments
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed with exit code ${LASTEXITCODE}: git $($GitArguments -join ' ')"
    }
}

$unpacklzexeCommit = "3a1b8b54e63e7e03181916d40acf7626d5558f6d"
$unpacker = Join-Path $vendor "unpacklzexe"
if (-not (Test-Path $unpacker)) {
    Invoke-Git @("clone", "https://github.com/samrussell/unpacklzexe.git", $unpacker)
}

Invoke-Git @("-C", $unpacker, "fetch", "origin", $unpacklzexeCommit)
Invoke-Git @("-C", $unpacker, "checkout", "--detach", $unpacklzexeCommit)

foreach ($tool in @("ghidraRun", "analyzeHeadless", "dosbox-x")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Warning "$tool is not on PATH; install the pinned version documented in analysis/README.md"
    }
}
