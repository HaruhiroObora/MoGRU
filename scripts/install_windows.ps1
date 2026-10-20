# ==============================================================================
# MoGRU Build & Install Script for Windows (PowerShell)
# ==============================================================================
# Run with: Set-ExecutionPolicy Bypass -Scope Process -Force; .\scripts\install_windows.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "=== MoGRU Installer for Windows ===" -ForegroundColor Cyan
Write-Host "Repository Root: $RepoRoot"

# 1. Check Administrator Rights (recommended for MSI installation)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Running without Administrator privileges. MSI installation may prompt for UAC elevation."
}

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

# 5. Run MSI Installation
Write-Host "=== Installing Mozc ===" -ForegroundColor Cyan
Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$MsiPath`" /qb" -Wait

# 6. Copy GRU model files to AppData\LocalLow\Google\JapaneseInput
$ModelDestDir = Join-Path $env:USERPROFILE "AppData\LocalLow\Google\JapaneseInput"
if (-not (Test-Path $ModelDestDir)) {
    New-Item -ItemType Directory -Path $ModelDestDir -Force | Out-Null
}

$ModelSrc = Join-Path $RepoRoot "models\grumodel.bin"
$ChardataSrc = Join-Path $RepoRoot "models\chardata.bin"

if ((Test-Path $ModelSrc) -and (Test-Path $ChardataSrc)) {
    Write-Host "[+] Copying GRU models to $ModelDestDir ..." -ForegroundColor Green
    Copy-Item -Path $ModelSrc -Destination $ModelDestDir -Force
    Copy-Item -Path $ChardataSrc -Destination $ModelDestDir -Force
}

Write-Host "=== Installation Complete! ===" -ForegroundColor Green
Write-Host "MoGRU (Mozc with GRU Language Model) is now installed on Windows."
Write-Host "You can switch to MoGRU using Windows Key + Space."
