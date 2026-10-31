# ==============================================================================
# MoGRU Installer for Windows (PowerShell)
# ==============================================================================
# Installs MoGRU from a prebuilt release package (zip, msi, or directory).
# Run with: Set-ExecutionPolicy Bypass -Scope Process -Force; .\install.ps1
# ==============================================================================

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir

Write-Host "=== MoGRU Installer for Windows ===" -ForegroundColor Cyan

$PackageArg = ""
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -in @("-h", "--help", "-Help")) {
        Write-Host "Usage: .\install.ps1 [ <path-to-MoGRU-windows-x64.zip-or-msi-or-dir> ]"
        Write-Host "       (When run inside extracted MoGRU package, arguments can be omitted)"
        exit 0
    } elseif ($args[$i] -in @("--binary", "-Binary", "-b")) {
        if ($i + 1 -lt $args.Count) {
            $PackageArg = $args[$i + 1]
            $i++
        }
    } elseif (-not $PackageArg) {
        $PackageArg = $args[$i]
    }
}

if (-not $PackageArg) {
    # Auto-detection when run without arguments inside extracted package or alongside zip
    $LocalMsi = Get-ChildItem -Path $ScriptDir -Filter "*.msi" -ErrorAction SilentlyContinue | Select-Object -First 1
    $LocalZip = Get-ChildItem -Path $ScriptDir -Filter "MoGRU-windows-*.zip" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($LocalMsi) {
        $PackageArg = $LocalMsi.FullName
        Write-Host "[+] Auto-detected MSI installer at: $($LocalMsi.FullName)" -ForegroundColor Green
    } elseif ($LocalZip) {
        $PackageArg = $LocalZip.FullName
        Write-Host "[+] Auto-detected package archive at: $($LocalZip.FullName)" -ForegroundColor Green
    } else {
        # Check current directory
        $CurrMsi = Get-ChildItem -Path "." -Filter "*.msi" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($CurrMsi) {
            $PackageArg = $CurrMsi.FullName
        } else {
            Write-Error "[!] No MoGRU package (.zip or .msi) found. Please run this script inside an extracted package or specify the path to MoGRU-windows-x64.zip."
            exit 1
        }
    }
}

# Check Administrator Rights
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Running without Administrator privileges. MSI installation may prompt for UAC elevation."
}

$MsiPath = ""
$TempExtractDir = ""
$PackageModelsDir = ""

try {
    if (-not (Test-Path -LiteralPath $PackageArg)) {
        Write-Error "[!] Specified package path not found: $PackageArg"
        exit 1
    }

    $ResolvedPath = (Resolve-Path -LiteralPath $PackageArg).Path
    $Item = Get-Item -LiteralPath $ResolvedPath

    if ($Item.PSIsContainer) {
        $FoundMsi = Get-ChildItem -Path $ResolvedPath -Filter "*.msi" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($FoundMsi) {
            $MsiPath = $FoundMsi.FullName
        } else {
            Write-Error "[!] No .msi installer found inside directory: $ResolvedPath"
            exit 1
        }
        $FoundModels = Get-ChildItem -Path $ResolvedPath -Filter "models" -Directory -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($FoundModels) {
            $PackageModelsDir = $FoundModels.FullName
        }
    } elseif ($ResolvedPath -match '\.zip$') {
        $TempExtractDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mogru_install_" + [guid]::NewGuid().ToString("N"))
        Write-Host "[+] Extracting package archive: $ResolvedPath" -ForegroundColor Cyan
        Expand-Archive -LiteralPath $ResolvedPath -DestinationPath $TempExtractDir -Force
        $FoundMsi = Get-ChildItem -Path $TempExtractDir -Filter "*.msi" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($FoundMsi) {
            $MsiPath = $FoundMsi.FullName
        } else {
            Write-Error "[!] No .msi installer found inside archive: $ResolvedPath"
            exit 1
        }
        $FoundModels = Get-ChildItem -Path $TempExtractDir -Filter "models" -Directory -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($FoundModels) {
            $PackageModelsDir = $FoundModels.FullName
        }
    } elseif ($ResolvedPath -match '\.msi$') {
        $MsiPath = $ResolvedPath
        $DirOfMsi = Split-Path -Parent $ResolvedPath
        $PossibleModels = Join-Path $DirOfMsi "models"
        if (Test-Path $PossibleModels) {
            $PackageModelsDir = $PossibleModels
        }
    } else {
        Write-Error "[!] Unexpected file format: $ResolvedPath (expected .zip or .msi)"
        exit 1
    }

    Write-Host "[+] Installing MSI: $MsiPath" -ForegroundColor Green

    # 1. Run MSI Installation
    Write-Host "=== Installing MoGRU ===" -ForegroundColor Cyan
    $Process = Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$MsiPath`" /qb" -Wait -PassThru
    if ($Process.ExitCode -ne 0 -and $Process.ExitCode -ne 3010) {
        Write-Error "msiexec.exe failed with exit code $($Process.ExitCode)."
        exit $Process.ExitCode
    }

    # 2. Install GRU model files to AppData\LocalLow\MoGRU
    # All files in models/ except gru_config.ini are unconditionally copied as model files
    $CandidateModelDirs = @(
        $PackageModelsDir,
        (Join-Path $ScriptDir "models"),
        (Join-Path $RepoRoot "models"),
        (Join-Path (Get-Location) "models")
    ) | Where-Object { $_ -and (Test-Path $_) }

    # Resolve unique candidate directories
    $UniqueDirs = @()
    foreach ($dir in $CandidateModelDirs) {
        $resolved = (Resolve-Path $dir -ErrorAction SilentlyContinue).Path
        if ($resolved -and (-not ($UniqueDirs -contains $resolved))) {
            $UniqueDirs += $resolved
        }
    }

    $TargetDirs = @(
        (Join-Path $env:USERPROFILE "AppData\LocalLow\MoGRU")
    )

    if ($UniqueDirs.Count -gt 0) {
        Write-Host "=== Installing MoGRU Models ===" -ForegroundColor Cyan
        foreach ($ModelDestDir in $TargetDirs) {
            if (-not (Test-Path $ModelDestDir)) {
                New-Item -ItemType Directory -Path $ModelDestDir -Force | Out-Null
            }

            # Unconditionally copy all files except gru_config.ini
            $CopiedCount = 0
            foreach ($d in $UniqueDirs) {
                $ModelFiles = Get-ChildItem -Path $d -File -ErrorAction SilentlyContinue
                foreach ($file in $ModelFiles) {
                    if ($file.Name -ne "gru_config.ini") {
                        Write-Host "[+] Copying model file: $($file.Name) (from $d) -> $ModelDestDir" -ForegroundColor Green
                        Copy-Item -Path $file.FullName -Destination $ModelDestDir -Force
                        $CopiedCount++
                    }
                }
            }

            if ($CopiedCount -eq 0) {
                Write-Warning "[!] No model files found in candidate directories: $($UniqueDirs -join ', ')"
            }

            # Copy or upgrade gru_config.ini from the first candidate directory that has it
            foreach ($d in $UniqueDirs) {
                $ConfigSrc = Join-Path $d "gru_config.ini"
                if (Test-Path $ConfigSrc) {
                    $ConfigDest = Join-Path $ModelDestDir "gru_config.ini"
                    if (-not (Test-Path $ConfigDest)) {
                        Write-Host "[+] Initializing $ConfigDest from default template ..." -ForegroundColor Green
                        Copy-Item -Path $ConfigSrc -Destination $ConfigDest -Force
                    } else {
                        $ExistingContent = Get-Content -Path $ConfigDest -Raw -ErrorAction SilentlyContinue
                        if ($ExistingContent -and (($ExistingContent -match "katakana_penalty") -or ($ExistingContent -notmatch "enable_live_conversion"))) {
                            Write-Host "[+] Upgrading $ConfigDest to unified MoGRU config format (backup saved to $ConfigDest.bak) ..." -ForegroundColor Green
                            Copy-Item -Path $ConfigDest -Destination "$ConfigDest.bak" -Force
                            Copy-Item -Path $ConfigSrc -Destination $ConfigDest -Force
                        } else {
                            Write-Host "[*] Preserving existing configuration: $ConfigDest" -ForegroundColor Gray
                        }
                    }
                    break
                }
            }
        }
    } else {
        Write-Warning "[!] No candidate model directories found."
    }
} finally {
    if ($TempExtractDir -and (Test-Path -LiteralPath $TempExtractDir)) {
        Remove-Item -LiteralPath $TempExtractDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 3. Clean up legacy MoGRU files from Mozc directories
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
