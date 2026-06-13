$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vendorPath = Join-Path $root "tools\vendor"
New-Item -ItemType Directory -Force -Path $vendorPath | Out-Null
$vendor = (Resolve-Path $vendorPath).Path
$downloads = Join-Path $vendor "downloads"

foreach ($tool in @("python", "cmake", "git", "cl")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool is not on PATH: $tool"
    }
}

$pinnedArchives = @(
    @{
        Name = "ghidra_12.1.2_PUBLIC_20260605.zip"
        Sha256 = "b62e81a0390618466c019c60d8c2f796ced2509c4c1aea4a37644a77272cf99d"
    },
    @{
        Name = "dosbox-x-mingw64-dosbox-x-v2026.06.02-osfree-portable.zip"
        Sha256 = "1c71c6e580a9b675029d0f40f3646bf86f59a4d47dff964974e8f2f6048b51f6"
    }
)

foreach ($archive in $pinnedArchives) {
    $archivePath = Join-Path $downloads $archive.Name
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        Write-Warning "Pinned archive is absent: $archivePath"
        continue
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $archive.Sha256) {
        throw "SHA-256 mismatch for $($archive.Name): expected $($archive.Sha256), got $actualHash"
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
