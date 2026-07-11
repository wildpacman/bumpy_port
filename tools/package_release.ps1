$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = "v0.1.0"
$stageName = "Bumpy3D-$version-win64"

Push-Location $root
try {
    $buildDir = "build/windows-debug/Release"
    $exe = Join-Path $buildDir "bumpy_port.exe"
    $dll = Join-Path $buildDir "SDL3.dll"

    if (-not (Test-Path $exe)) {
        throw "bumpy_port.exe not found at $exe -- run: cmake --build --preset windows-release"
    }

    $stage = "dist/$stageName"
    if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" -Confirm:$false }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    Copy-Item $exe (Join-Path $stage "Bumpy3D.exe")
    Copy-Item $dll $stage
    Copy-Item -Recurse "shaders3d" (Join-Path $stage "shaders3d")
    Copy-Item -Recurse "config" (Join-Path $stage "config")
    Copy-Item "LICENSE" $stage
    Copy-Item "tools/reference/PLAY.txt" $stage

    $zip = "dist/$stageName.zip"
    Compress-Archive -Path "$stage/*" -DestinationPath $zip -Force

    Write-Output "Packaged: $zip"
} finally {
    Pop-Location
}
