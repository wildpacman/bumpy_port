param(
    [switch] $VerifyMenu,
    [ValidateRange(5, 120)]
    [int] $StartupTimeoutSeconds = 20
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$archive = Join-Path $root "tools\vendor\downloads\dosbox-x-mingw64-2026.06.02-portable.zip"
$archiveSha256 = "be4faa5edd5980159ed4dfa8c803269beb29a58f02190b6b3ee1a8f52ae57235"
$dosboxSha256 = "a335e2b3e0439e90183ece3a6aa7bcfe7fc527c7ff388ee1b7dc6352f934a6f5"
$expectedVersion = "2026.06.02"
$config = Join-Path $root "reference\dosbox-x.conf"
$generated = Join-Path $root "analysis\generated\reference"
$dosboxInstall = Join-Path $generated "dosbox-x"
$dosbox = Join-Path $dosboxInstall "mingw-build\mingw\dosbox-x.exe"
$runDirectory = Join-Path $generated "run"
$captureDirectory = Join-Path $generated "capture"
$manifest = Join-Path $root "config\original-assets.sha256"

function Assert-Hash {
    param(
        [Parameter(Mandatory)]
        [string] $Path,
        [Parameter(Mandatory)]
        [string] $Expected
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required pinned file is absent: $Path"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for ${Path}: expected $Expected, got $actual"
    }
}

function Assert-OriginalAssets {
    & python (Join-Path $root "tools\assets\manifest.py") verify --root $root --manifest $manifest
    if ($LASTEXITCODE -ne 0) {
        throw "original asset verification failed"
    }
}

function Copy-OriginalAssets {
    foreach ($line in Get-Content -LiteralPath $manifest -Encoding ascii) {
        $name = $line.Substring(66)
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $runDirectory $name)
    }
}

function Stop-ReferenceProcess {
    param([System.Diagnostics.Process] $Process)

    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
}

Assert-OriginalAssets
Write-Output "Original asset verification passed before launch"

Assert-Hash -Path $archive -Expected $archiveSha256
Remove-Item -LiteralPath $generated -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $runDirectory, $captureDirectory -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $dosboxInstall
Assert-Hash -Path $dosbox -Expected $dosboxSha256
$version = (Get-Item -LiteralPath $dosbox).VersionInfo.FileVersion
if ($version -ne $expectedVersion) {
    throw "DOSBox-X version mismatch: expected $expectedVersion, got $version"
}
Write-Output "DOSBox-X version verified: $version"
Copy-OriginalAssets

$arguments = @(
    "-conf", $config,
    "-set", "captures=$captureDirectory",
    "-set", "mapperfile=$(Join-Path $generated 'mapper.map')",
    (Join-Path $runDirectory "BUMPY.EXE")
)

Push-Location $runDirectory
try {
    if (-not $VerifyMenu) {
        & $dosbox @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "DOSBox-X exited with code $LASTEXITCODE"
        }
    } else {
        $process = Start-Process -FilePath $dosbox -ArgumentList $arguments -WorkingDirectory $runDirectory -PassThru
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
            while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited -and $process.MainWindowHandle -eq 0) {
                Start-Sleep -Milliseconds 100
                $process.Refresh()
            }
            if ($process.HasExited) {
                throw "DOSBox-X exited before the menu probe"
            }
            if ($process.MainWindowHandle -eq 0) {
                throw "DOSBox-X did not create a window within $StartupTimeoutSeconds seconds"
            }

            Start-Sleep -Seconds 5
            $process.Refresh()
            if ($process.MainWindowTitle -notlike "DOSBox-X ${expectedVersion}: BUMPY - 3000 cycles*") {
                throw "Bumpy did not reach its startup menu; unexpected DOSBox-X title: $($process.MainWindowTitle)"
            }
            Add-Type -AssemblyName System.Drawing
            Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ReferenceWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);
}
"@
            $rect = New-Object ReferenceWindow+Rect
            if (-not [ReferenceWindow]::GetWindowRect($process.MainWindowHandle, [ref] $rect)) {
                throw "Could not locate the DOSBox-X window"
            }
            $width = $rect.Right - $rect.Left
            $height = $rect.Bottom - $rect.Top
            $screenshotPath = Join-Path $captureDirectory "menu.png"
            $image = New-Object System.Drawing.Bitmap $width, $height
            $graphics = [System.Drawing.Graphics]::FromImage($image)
            try {
                $deviceContext = $graphics.GetHdc()
                try {
                    if (-not [ReferenceWindow]::PrintWindow($process.MainWindowHandle, $deviceContext, 2)) {
                        throw "Could not capture the DOSBox-X window"
                    }
                } finally {
                    $graphics.ReleaseHdc($deviceContext)
                }
                $image.Save($screenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
            } finally {
                $graphics.Dispose()
            }
            try {
                if ($image.Width -lt 320 -or $image.Height -lt 200) {
                    throw "Reference screenshot is too small: $($image.Width)x$($image.Height)"
                }
                $colors = [System.Collections.Generic.HashSet[int]]::new()
                for ($y = 0; $y -lt $image.Height; $y += 8) {
                    for ($x = 0; $x -lt $image.Width; $x += 8) {
                        [void] $colors.Add($image.GetPixel($x, $y).ToArgb())
                    }
                }
                if ($colors.Count -lt 8) {
                    throw "Reference screenshot is not a non-trivial game menu image"
                }
            } finally {
                $image.Dispose()
            }
            $screenshotHash = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
            Write-Output "Reference menu probe passed: $($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top) screenshot SHA-256 $screenshotHash"
        } finally {
            Stop-ReferenceProcess -Process $process
        }
    }
} finally {
    Pop-Location
    Assert-OriginalAssets
    Write-Output "Original asset verification passed after launch"
}
