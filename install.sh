#!/usr/bin/env bash
# ==============================================================================
# Mozc GRU Unified Cross-Platform Installer Entrypoint
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OS_NAME="$(uname -s 2>/dev/null || echo "Unknown")"

echo "=== MoGRU (Neural Language Model) Installer ==="
echo "Detected Operating System: $OS_NAME"

case "$OS_NAME" in
  Linux*)
    echo "Routing to Linux (Fcitx5) installer..."
    bash "$SCRIPT_DIR/scripts/install_linux.sh" "$@"
    ;;
  Darwin*)
    echo "Routing to macOS installer..."
    bash "$SCRIPT_DIR/scripts/install_macos.sh" "$@"
    ;;
  CYGWIN*|MINGW*|MSYS*)
    echo "Detected Windows environment."
    if command -v powershell.exe &>/dev/null; then
      echo "Routing to Windows (PowerShell) installer..."
      PS_SCRIPT="$SCRIPT_DIR/scripts/install_windows.ps1"
      if command -v cygpath &>/dev/null; then
        PS_SCRIPT="$(cygpath -w "$PS_SCRIPT")"
        ARGS=()
        for arg in "$@"; do
          if [ -e "$arg" ]; then
            ARGS+=("$(cygpath -w "$arg")")
          else
            ARGS+=("$arg")
          fi
        done
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS_SCRIPT" "${ARGS[@]}"
      else
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS_SCRIPT" "$@"
      fi
    else
      echo "Please run the PowerShell installer in an Administrator PowerShell console:"
      echo "    Set-ExecutionPolicy Bypass -Scope Process -Force; .\\scripts\\install_windows.ps1 $*"
      exit 1
    fi
    ;;
  *)
    echo "[!] Unsupported or unrecognized OS: $OS_NAME"
    echo "    Available scripts in scripts/:"
    echo "    - scripts/install_linux.sh"
    echo "    - scripts/install_macos.sh"
    echo "    - scripts/install_windows.ps1"
    exit 1
    ;;
esac
