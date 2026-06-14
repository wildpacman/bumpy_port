$ErrorActionPreference = "Stop"

$GhidraArchiveName = "ghidra_12.1.2_PUBLIC_20260605.zip"
$GhidraArchiveSha256 = "b62e81a0390618466c019c60d8c2f796ced2509c4c1aea4a37644a77272cf99d"
$GhidraVersion = "12.1.2"
$JdkArchiveName = "OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip"
$JdkArchiveSha256 = "d3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64"
$JdkVersion = "21.0.11"
$PythonVersion = "3.12.0"
$PyLauncherSha256 = "bec50779367301f008aec0066595582275c4db949e4789eda9a87050c08905f4"
$PythonExecutableSha256 = "42ac541168e97dedb9aabd8be335539fc41c682e414b9e8d137b164fb68683b0"
$PyGhidraVersion = "3.1.0"
$PyGhidraWheelSha256 = "d4d21729c126190ca358700220fed62af4be2252b4e255ffb889d82dd5a263ac"
$Loader = "MzLoader"
$LoaderDisplay = "Old-style DOS Executable (MZ)"
$Language = "x86:LE:16:Real Mode:default"
$WarningSignature = "Decompiling 1000:35a5, pcode error at 1000:84d7: Unable to resolve constructor at 1000:84d7"

function Get-Sha256 {
    param([Parameter(Mandatory)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Expected
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "required pinned file is absent: $Path"
    }
    $actual = Get-Sha256 $Path
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path`: expected $Expected, got $actual"
    }
}

function Test-FileHash {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Expected
    )
    try {
        return (Test-Path -LiteralPath $Path -PathType Leaf) -and
            ((Get-Sha256 $Path) -eq $Expected)
    }
    catch {
        return $false
    }
}

function Assert-ZipInstallFile {
    param(
        [Parameter(Mandatory)][string] $Archive,
        [Parameter(Mandatory)][string] $EntryName,
        [Parameter(Mandatory)][string] $InstalledPath
    )
    if (-not (Test-Path -LiteralPath $InstalledPath -PathType Leaf)) {
        throw "required pinned install file is absent: $InstalledPath"
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entry = $zip.GetEntry($EntryName)
        if ($null -eq $entry) {
            throw "pinned archive lacks expected entry: $EntryName"
        }
        $stream = $entry.Open()
        try {
            $sha = [Security.Cryptography.SHA256]::Create()
            try {
                $archiveHash = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant()
            }
            finally {
                $sha.Dispose()
            }
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $zip.Dispose()
    }
    $installedHash = Get-Sha256 $InstalledPath
    if ($archiveHash -ne $installedHash) {
        throw "installed file differs from pinned archive: $InstalledPath"
    }
}

function Get-PinnedPython {
    $launchers = @(Get-Command py -CommandType Application -ErrorAction SilentlyContinue)
    if ($launchers.Count -eq 0) {
        throw "required Windows py launcher is absent"
    }
    $matchingLaunchers = @(
        $launchers | Where-Object {
            Test-FileHash $_.Source $PyLauncherSha256
        }
    )
    if ($matchingLaunchers.Count -ne 1) {
        throw "expected exactly one pinned Windows py launcher, found $($matchingLaunchers.Count)"
    }
    $launcher = $matchingLaunchers[0]

    $version = (& $launcher.Source -3.12 -c "import platform; print(platform.python_version())").Trim()
    if ($LASTEXITCODE -ne 0 -or $version -ne $PythonVersion) {
        throw "Python prerequisite mismatch: expected $PythonVersion via py -3.12, got $version"
    }
    $executable = (& $launcher.Source -3.12 -c "import sys; print(sys.executable)").Trim()
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Python 3.12 executable could not be resolved via py launcher"
    }
    Assert-FileHash $executable $PythonExecutableSha256
    return [ordered]@{
        launcher = $launcher.Source
        launcher_sha256 = $PyLauncherSha256
        executable = $executable
        executable_sha256 = $PythonExecutableSha256
        version = $version
    }
}

function Remove-SafeGeneratedDirectory {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $GeneratedRoot
    )
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = [IO.Path]::GetFullPath($GeneratedRoot).TrimEnd("\") + "\"
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing to remove path outside analysis/generated: $full"
    }
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $Purpose,
        [Parameter(Mandatory)][string] $Executable,
        [Parameter(Mandatory)][string[]] $Arguments
    )
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Purpose failed with exit code $LASTEXITCODE"
    }
}

function New-SafeAlias {
    param(
        [Parameter(Mandatory)][string] $Alias,
        [Parameter(Mandatory)][string] $Target
    )
    if (Test-Path -LiteralPath $Alias) {
        $item = Get-Item -LiteralPath $Alias -Force
        $actualTarget = [IO.Path]::GetFullPath([string]$item.Target)
        if ($item.LinkType -ne "Junction" -or
            $actualTarget -ne [IO.Path]::GetFullPath($Target)) {
            throw "refusing to reuse unexpected alias: $Alias"
        }
        return
    }
    New-Item -ItemType Junction -Path $Alias -Target $Target | Out-Null
}

function Remove-SafeAlias {
    param(
        [Parameter(Mandatory)][string] $Alias,
        [Parameter(Mandatory)][string] $Target
    )
    if (-not (Test-Path -LiteralPath $Alias)) {
        return
    }
    $item = Get-Item -LiteralPath $Alias -Force
    $actualTarget = [IO.Path]::GetFullPath([string]$item.Target)
    if ($item.LinkType -ne "Junction" -or
        $actualTarget -ne [IO.Path]::GetFullPath($Target)) {
        throw "refusing to remove unexpected alias: $Alias"
    }
    Remove-Item -LiteralPath $Alias -Force
}

function Invoke-CleanImport {
    param(
        [Parameter(Mandatory)][int] $Number,
        [Parameter(Mandatory)][string] $Alias,
        [Parameter(Mandatory)][string] $Python,
        [Parameter(Mandatory)][string] $Launcher,
        [Parameter(Mandatory)][string] $GhidraInstall,
        [Parameter(Mandatory)][string] $GeneratedRoot
    )
    $name = "ghidra-clean-$Number"
    $physicalProject = Join-Path $GeneratedRoot $name
    Remove-SafeGeneratedDirectory $physicalProject $GeneratedRoot

    $project = Join-Path $Alias "analysis\generated\$name"
    $input = Join-Path $Alias "analysis\generated\BUMPY.UNPACKED.EXE"
    $scripts = Join-Path $Alias "analysis\ghidra_scripts"
    $discovery = Join-Path $Alias "analysis\generated\$name-functions.csv"
    $log = Join-Path $Alias "analysis\generated\$name.log"
    $scriptLog = Join-Path $Alias "analysis\generated\$name-script.log"
    foreach ($file in @($discovery, $log, $scriptLog)) {
        if (Test-Path -LiteralPath $file) {
            Remove-Item -LiteralPath $file -Force
        }
    }
    New-Item -ItemType Directory -Force -Path $project | Out-Null

    Invoke-Checked "Ghidra clean import $Number" $Python @(
        $Launcher, $GhidraInstall, "-H", $project, "Bumpy",
        "-import", $input,
        "-loader", $Loader,
        "-processor", "x86:LE:16:Real Mode",
        "-analysisTimeoutPerFile", "900",
        "-scriptPath", $scripts,
        "-postScript", "ExportFunctions.py", $discovery,
        "-log", $log,
        "-scriptlog", $scriptLog
    )

    $logText = Get-Content -LiteralPath $log -Raw
    foreach ($required in @(
        "Using Loader: $LoaderDisplay",
        "Using Language/Compiler: $Language",
        "REPORT: Analysis succeeded",
        "REPORT: Import succeeded"
    )) {
        if (-not $logText.Contains($required)) {
            throw "Ghidra clean import $Number log lacks required evidence: $required"
        }
    }
    if ($logText.Contains(" ERROR ")) {
        throw "Ghidra clean import $Number log contains an error"
    }
    $warningLines = @(
        Get-Content -LiteralPath $log |
            Where-Object { $_ -match " WARN " }
    )
    if ($warningLines.Count -ne 2) {
        throw "Ghidra clean import $Number produced $($warningLines.Count) warnings, expected 2"
    }
    foreach ($line in $warningLines) {
        if (-not $line.Contains($WarningSignature)) {
            throw "Ghidra clean import $Number produced an unallowlisted warning: $line"
        }
    }

    return [ordered]@{
        name = $name
        discovery_sha256 = Get-Sha256 $discovery
        function_count = @(Import-Csv -LiteralPath $discovery).Count
        warning_count = $warningLines.Count
    }
}

function Write-JsonAtomic {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)] $Value
    )
    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $temporary = Join-Path $directory ".$([IO.Path]::GetFileName($Path)).$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        $json = ($Value | ConvertTo-Json -Depth 10) + "`n"
        [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary, $Path, $true)
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Invoke-Main {
    $root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
    Set-Location -LiteralPath $root
    $vendor = Join-Path $root "tools\vendor"
    $downloads = Join-Path $vendor "downloads"
    $generated = Join-Path $root "analysis\generated"
    $ghidraArchive = Join-Path $downloads $GhidraArchiveName
    $jdkArchive = Join-Path $downloads $JdkArchiveName
    $toolRoot = Join-Path $generated "ghidra-tools"
    $ghidraInstall = Join-Path $toolRoot "ghidra_12.1.2_PUBLIC"
    $jdkInstall = Join-Path $toolRoot "jdk-21.0.11+10"
    $wheel = Join-Path $ghidraInstall "Ghidra\Features\PyGhidra\pypkg\dist\pyghidra-3.1.0-py3-none-any.whl"
    $venv = Join-Path $toolRoot "pyghidra-venv"
    $venvPython = Join-Path $venv "Scripts\python.exe"
    $launcher = Join-Path $ghidraInstall "Ghidra\Features\PyGhidra\support\pyghidra_launcher.py"
    $unpacked = Join-Path $generated "BUMPY.UNPACKED.EXE"
    $catalog = Join-Path $root "analysis\catalog\functions.csv"
    $addresses = Join-Path $root "analysis\catalog\function_addresses.csv"
    $report = Join-Path $root "analysis\reports\ghidra-analysis.json"
    $aliasRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $root))
    $alias = Join-Path $aliasRoot "BUMPY_GHIDRA_ALIAS"

    Assert-FileHash $ghidraArchive $GhidraArchiveSha256
    Assert-FileHash $jdkArchive $JdkArchiveSha256
    $pythonPrerequisite = Get-PinnedPython
    Remove-SafeGeneratedDirectory $toolRoot $generated
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    Expand-Archive -LiteralPath $ghidraArchive -DestinationPath $toolRoot
    Expand-Archive -LiteralPath $jdkArchive -DestinationPath $toolRoot

    Assert-FileHash $wheel $PyGhidraWheelSha256
    foreach ($required in @($launcher, (Join-Path $jdkInstall "bin\java.exe"))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "required pinned install file is absent: $required"
        }
    }
    Assert-ZipInstallFile $ghidraArchive `
        "ghidra_12.1.2_PUBLIC/Ghidra/application.properties" `
        (Join-Path $ghidraInstall "Ghidra\application.properties")
    Assert-ZipInstallFile $ghidraArchive `
        "ghidra_12.1.2_PUBLIC/Ghidra/Features/PyGhidra/pypkg/dist/pyghidra-3.1.0-py3-none-any.whl" `
        $wheel
    Assert-ZipInstallFile $jdkArchive "jdk-21.0.11+10/release" `
        (Join-Path $jdkInstall "release")
    Assert-ZipInstallFile $jdkArchive "jdk-21.0.11+10/bin/java.exe" `
        (Join-Path $jdkInstall "bin\java.exe")
    $properties = Get-Content -LiteralPath (Join-Path $ghidraInstall "Ghidra\application.properties") -Raw
    if (-not $properties.Contains("application.version=$GhidraVersion")) {
        throw "Ghidra install version mismatch"
    }
    $release = Get-Content -LiteralPath (Join-Path $jdkInstall "release") -Raw
    if (-not $release.Contains("JAVA_VERSION=`"$JdkVersion`"")) {
        throw "JDK install version mismatch"
    }

    Invoke-Checked "asset verification" $pythonPrerequisite.executable @(
        (Join-Path $root "tools\assets\manifest.py"), "verify"
    )
    Invoke-Checked "validated unpacking" $pythonPrerequisite.executable @(
        (Join-Path $root "tools\re\validate_unpack.py")
    )
    Assert-FileHash $unpacked "3ff2f60b474dc04b1de7c69cf3764b95e31967b74a00f755d231ddd3235adbe0"

    Invoke-Checked "create clean PyGhidra venv" $pythonPrerequisite.launcher @(
        "-3.12", "-m", "venv", $venv
    )
    Invoke-Checked "install pinned PyGhidra" $venvPython @(
        "-m", "pip", "install", "--disable-pip-version-check", "--no-index",
        "--force-reinstall", "--find-links", (Split-Path -Parent $wheel),
        "pyghidra==$PyGhidraVersion"
    )
    $installedVersion = (& $venvPython -c "import pyghidra; print(pyghidra.__version__)").Trim()
    if ($LASTEXITCODE -ne 0 -or $installedVersion -ne $PyGhidraVersion) {
        throw "installed PyGhidra version mismatch: $installedVersion"
    }
    Invoke-Checked "verify PyGhidra environment" $venvPython @("-m", "pip", "check")

    $oldJavaHome = $env:JAVA_HOME
    try {
        $env:JAVA_HOME = $jdkInstall
        New-SafeAlias $alias $root
        $first = Invoke-CleanImport 1 $alias $venvPython $launcher $ghidraInstall $generated
        $second = Invoke-CleanImport 2 $alias $venvPython $launcher $ghidraInstall $generated
        if ($first.discovery_sha256 -ne $second.discovery_sha256) {
            throw "clean Ghidra imports produced different discovery hashes"
        }
        if ($first.function_count -ne $second.function_count) {
            throw "clean Ghidra imports produced different function counts"
        }

        $discovery = Join-Path $generated "ghidra-clean-1-functions.csv"
        Invoke-Checked "publish curated Ghidra catalog" $venvPython @(
            (Join-Path $root "tools\re\ghidra_catalog.py"),
            $discovery, $catalog, $addresses
        )
        $functionCount = @(Import-Csv -LiteralPath $catalog).Count
        $reportValue = [ordered]@{
            schema_version = 1
            loader = $Loader
            loader_display = $LoaderDisplay
            language = $Language
            input_sha256 = Get-Sha256 $unpacked
            discovery_sha256 = $first.discovery_sha256
            catalog_sha256 = Get-Sha256 $catalog
            address_catalog_sha256 = Get-Sha256 $addresses
            function_count = $functionCount
            discovered_function_count = $first.function_count
            tools = [ordered]@{
                ghidra_version = $GhidraVersion
                ghidra_archive_sha256 = $GhidraArchiveSha256
                jdk_version = $JdkVersion
                jdk_archive_sha256 = $JdkArchiveSha256
                python_version = $pythonPrerequisite.version
                py_launcher_sha256 = $pythonPrerequisite.launcher_sha256
                python_executable_sha256 = $pythonPrerequisite.executable_sha256
                pyghidra_version = $PyGhidraVersion
                pyghidra_wheel_sha256 = $PyGhidraWheelSha256
                clean_archive_extraction_per_run = $true
                ghidra_install = "analysis/generated/ghidra-tools/ghidra_12.1.2_PUBLIC"
                jdk_install = "analysis/generated/ghidra-tools/jdk-21.0.11+10"
            }
            clean_imports = @($first, $second)
            warning = [ordered]@{
                status = "allowlisted known Ghidra decompiler warning"
                signature = $WarningSignature
                count_per_import = 2
                confirmed_effect = "warning is present while headless analysis and import report success; no broader effect has been established"
            }
        }
        Write-JsonAtomic $report $reportValue
    }
    finally {
        $env:JAVA_HOME = $oldJavaHome
        Remove-SafeAlias $alias $root
    }

    Invoke-Checked "post-analysis asset verification" $pythonPrerequisite.executable @(
        (Join-Path $root "tools\assets\manifest.py"), "verify"
    )
}

$startingLocation = (Get-Location).Path
$exitCode = 0
try {
    Invoke-Main
}
catch {
    [Console]::Error.WriteLine("error: $($_.Exception.Message)")
    $exitCode = 2
}
finally {
    Set-Location -LiteralPath $startingLocation
}
exit $exitCode
