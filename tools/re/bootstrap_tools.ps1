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

function Set-PinnedCheckout {
    param(
        [Parameter(Mandatory)]
        [string] $Name,
        [Parameter(Mandatory)]
        [string] $Url,
        [Parameter(Mandatory)]
        [string] $Commit
    )

    $checkout = Join-Path $vendor $Name
    if (-not (Test-Path $checkout)) {
        Invoke-Git @("clone", $Url, $checkout)
    }

    $status = @(& git -C $checkout status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed with exit code ${LASTEXITCODE}: git -C $checkout status --porcelain"
    }
    if ($status.Count -ne 0) {
        throw "Vendor checkout must be pristine: $checkout contains tracked modifications or untracked files. Review and remove them manually; bootstrap will not reset or clean the checkout."
    }

    Invoke-Git @("-C", $checkout, "fetch", "origin", $Commit)
    Invoke-Git @("-C", $checkout, "checkout", "--detach", $Commit)

    $status = @(& git -C $checkout status --porcelain)
    if ($LASTEXITCODE -ne 0 -or $status.Count -ne 0) {
        throw "Pinned vendor checkout is not pristine: $checkout"
    }
}

Set-PinnedCheckout `
    -Name "unpacklzexe" `
    -Url "https://github.com/samrussell/unpacklzexe.git" `
    -Commit "3a1b8b54e63e7e03181916d40acf7626d5558f6d"

foreach ($tool in @("ghidraRun", "analyzeHeadless", "dosbox-x")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Warning "$tool is not on PATH; install the pinned version documented in analysis/README.md"
    }
}
