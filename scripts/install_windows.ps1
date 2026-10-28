# ==============================================================================
# MoGRU Build & Install Script for Windows (PowerShell)
# ==============================================================================
# Run with: Set-ExecutionPolicy Bypass -Scope Process -Force; .\scripts\install_windows.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "=== MoGRU Installer for Windows ===" -ForegroundColor Cyan
Write-Host "Repository Root: $RepoRoot"

$PrebuiltArg = ""
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -in @("-h", "--help", "-Help")) {
        Write-Host "Usage: .\scripts\install_windows.ps1 [ [--binary | -Binary] <path-to-Mozc64.msi-or-zip> ]"
        exit 0
    } elseif ($args[$i] -in @("--binary", "-Binary", "-b")) {
        if ($i + 1 -lt $args.Count) {
            $PrebuiltArg = $args[$i + 1]
            $i++
        } else {
            Write-Error "Missing argument for $($args[$i])"
            exit 1
        }
    } elseif (-not $PrebuiltArg) {
        $PrebuiltArg = $args[$i]
    }
}

# 1. Check Administrator Rights (recommended for MSI installation)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Running without Administrator privileges. MSI installation may prompt for UAC elevation."
}

$MsiPath = ""
$TempExtractDir = ""

try {
    if ($PrebuiltArg) {
        if (-not (Test-Path -LiteralPath $PrebuiltArg)) {
            Write-Error "[!] Prebuilt installer not found: $PrebuiltArg"
            exit 1
        }
        $ResolvedPath = (Resolve-Path -LiteralPath $PrebuiltArg).Path
        $Item = Get-Item -LiteralPath $ResolvedPath

        if ($Item.PSIsContainer) {
            $FoundMsi = Get-ChildItem -Path $ResolvedPath -Filter "*.msi" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($FoundMsi) {
                $MsiPath = $FoundMsi.FullName
            } else {
                Write-Error "[!] No .msi installer found inside directory: $ResolvedPath"
                exit 1
            }
        } elseif ($ResolvedPath -match '\.zip$') {
            $TempExtractDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mogru_install_" + [guid]::NewGuid().ToString("N"))
            Write-Host "[+] Extracting CI artifact archive: $ResolvedPath" -ForegroundColor Cyan
            Expand-Archive -LiteralPath $ResolvedPath -DestinationPath $TempExtractDir -Force
            $FoundMsi = Get-ChildItem -Path $TempExtractDir -Filter "*.msi" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($FoundMsi) {
                $MsiPath = $FoundMsi.FullName
            } else {
                Write-Error "[!] No .msi installer found inside archive: $ResolvedPath"
                exit 1
            }
        } else {
            $MsiPath = $ResolvedPath
        }

        Write-Host "[+] Using prebuilt installer from CI/Release: $MsiPath" -ForegroundColor Green
        Write-Host "    Skipping Bazel build step."
    } else {
        # 2. Check Bazelisk / Bazel
        $BazelBin = ""
        if (Get-Command "bazelisk" -ErrorAction SilentlyContinue) {
            $BazelBin = "bazelisk"
        } elseif (Get-Command "bazel" -ErrorAction SilentlyContinue) {
            $BazelBin = "bazel"
        } else {
            Write-Host "[!] Bazel or Bazelisk not found in PATH." -ForegroundColor Yellow
            Write-Host "    Attempting to install Bazelisk via winget..."
            if (Get-Command "winget" -ErrorAction SilentlyContinue) {
                winget install --id Bazel.Bazelisk -e --accept-source-agreements --accept-package-agreements
                $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
                $BazelBin = "bazelisk"
            } else {
                Write-Error "Please install Bazelisk (e.g. winget install Bazel.Bazelisk) and rerun this script."
                exit 1
            }
        }
        Write-Host "[+] Using Bazel: $BazelBin" -ForegroundColor Green

        # 3. Check Visual Studio C++ Environment
        $VSWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $VSWhere) {
            $VSPath = & $VSWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($VSPath) {
                Write-Host "[+] Found Visual Studio at: $VSPath" -ForegroundColor Green
            } else {
                Write-Warning "Visual Studio with C++ tools was not detected. Ensure 'Desktop development with C++' is installed."
            }
        }

        # 4. Build Mozc Installer for Windows
        Write-Host "=== Building Mozc Installer (-c opt) ===" -ForegroundColor Cyan
        Set-Location -Path "$RepoRoot\src"

        # Execute Bazel build
        & $BazelBin build -c opt //win32/installer:installer_oss

        $MsiPath = "$RepoRoot\src\bazel-bin\win32\installer\mozc64.msi"
        if (-not (Test-Path $MsiPath)) {
            # Check alternate naming
            $MsiFiles = Get-ChildItem -Path "$RepoRoot\src\bazel-bin\win32\installer" -Filter "*.msi" -ErrorAction SilentlyContinue
            if ($MsiFiles.Count -gt 0) {
                $MsiPath = $MsiFiles[0].FullName
            } else {
                Write-Error "Build finished but installer MSI was not found."
                exit 1
            }
        }

        Write-Host "[+] Built installer: $MsiPath" -ForegroundColor Green
    }

    # 5. Run MSI Installation
    Write-Host "=== Installing Mozc ===" -ForegroundColor Cyan
    $Process = Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$MsiPath`" /qb" -Wait -PassThru
    if ($Process.ExitCode -ne 0 -and $Process.ExitCode -ne 3010) {
        Write-Error "msiexec.exe failed with exit code $($Process.ExitCode)."
        exit $Process.ExitCode
    }
} finally {
    if ($TempExtractDir -and (Test-Path -LiteralPath $TempExtractDir)) {
        Remove-Item -LiteralPath $TempExtractDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 6. Copy GRU model files to AppData\LocalLow\Mozc and AppData\LocalLow\Google\JapaneseInput
$ModelSrc = Join-Path $RepoRoot "models\grumodel.bin"
$ChardataSrc = Join-Path $RepoRoot "models\chardata.bin"
$ConfigSrc = Join-Path $RepoRoot "models\gru_config.ini"

$TargetDirs = @(
    (Join-Path $env:USERPROFILE "AppData\LocalLow\MoGRU")
)

foreach ($ModelDestDir in $TargetDirs) {
    if (-not (Test-Path $ModelDestDir)) {
        New-Item -ItemType Directory -Path $ModelDestDir -Force | Out-Null
    }

    if ((Test-Path $ModelSrc) -and (Test-Path $ChardataSrc)) {
        Write-Host "[+] Copying GRU models to $ModelDestDir ..." -ForegroundColor Green
        Copy-Item -Path $ModelSrc -Destination $ModelDestDir -Force
        Copy-Item -Path $ChardataSrc -Destination $ModelDestDir -Force
    }

    $ConfigDest = Join-Path $ModelDestDir "gru_config.ini"
    if (Test-Path $ConfigSrc) {
        if (-not (Test-Path $ConfigDest)) {
            Write-Host "[+] Initializing $ConfigDest from default template ..." -ForegroundColor Green
            Copy-Item -Path $ConfigSrc -Destination $ConfigDest -Force
        } else {
            $ExistingContent = Get-Content -Path $ConfigDest -Raw -ErrorAction SilentlyContinue
            if ($ExistingContent -and (($ExistingContent -match "katakana_penalty") -or ($ExistingContent -notmatch "enable_live_conversion"))) {
                Write-Host "[+] Upgrading $ConfigDest to unified MoGRU config format (backup saved to $ConfigDest.bak) ..." -ForegroundColor Green
                Copy-Item -Path $ConfigDest -Destination "$ConfigDest.bak" -Force
                Copy-Item -Path $ConfigSrc -Destination $ConfigDest -Force
            }
        }
    }
}

# 7. Clean up legacy MoGRU files from Mozc directories to remove pollution
$LegacyDirs = @(
    (Join-Path $env:USERPROFILE "AppData\LocalLow\Mozc"),
    (Join-Path $env:USERPROFILE "AppData\LocalLow\Google\JapaneseInput")
)
foreach ($LegacyDir in $LegacyDirs) {
    if (Test-Path $LegacyDir) {
        Remove-Item -Path (Join-Path $LegacyDir "grumodel.bin") -Force -ErrorAction SilentlyContinue
        Remove-Item -Path (Join-Path $LegacyDir "chardata.bin") -Force -ErrorAction SilentlyContinue
        Remove-Item -Path (Join-Path $LegacyDir "gru_config.ini") -Force -ErrorAction SilentlyContinue
        Remove-Item -Path (Join-Path $LegacyDir "gru_config.ini.bak") -Force -ErrorAction SilentlyContinue
        Remove-Item -Path (Join-Path $LegacyDir "grumodel_user.bin") -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "=== Installation Complete! ===" -ForegroundColor Green
Write-Host "MoGRU (Mozc with GRU Language Model) is now installed on Windows."
Write-Host "You can switch to MoGRU using Windows Key + Space."
