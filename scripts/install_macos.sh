#!/usr/bin/env bash
# ==============================================================================
# Mozc GRU Build & Install Script for macOS
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Mozc GRU Installer for macOS ==="
echo "Repository Root: $REPO_ROOT"

# 1. Check OS
if [[ "$(uname)" != "Darwin" ]]; then
  echo "[Error] This script must be run on macOS."
  exit 1
fi

# 2. Check Xcode Command Line Tools
if ! xcode-select -p &>/dev/null; then
  echo "[!] Xcode Command Line Tools not found. Installing..."
  xcode-select --install
  echo "Please rerun this script after Xcode tools installation completes."
  exit 1
fi

# 3. Check Bazel / Bazelisk
BAZEL_BIN=""
if command -v bazelisk &>/dev/null; then
  BAZEL_BIN="bazelisk"
elif command -v bazel &>/dev/null; then
  BAZEL_BIN="bazel"
elif command -v brew &>/dev/null; then
  echo "[!] Installing bazelisk via Homebrew..."
  brew install bazelisk
  BAZEL_BIN="bazelisk"
else
  echo "[Error] Bazelisk not found. Please install Homebrew and run: brew install bazelisk"
  exit 1
fi
echo "[+] Using Bazel: $("$BAZEL_BIN" --version 2>/dev/null || echo "$BAZEL_BIN")"

# 4. Build Mozc.app for macOS
echo "=== Building Mozc.app (-c opt) ==="
cd "$REPO_ROOT/src"
"$BAZEL_BIN" build -c opt //mac:Mozc

# 5. Install Mozc.app to ~/Library/Input Methods
TARGET_DIR="$HOME/Library/Input Methods"
mkdir -p "$TARGET_DIR"

echo "=== Installing Mozc.app to $TARGET_DIR ==="
# Terminate running Mozc processes
killall MozcConverter 2>/dev/null || true
killall MozcRenderer 2>/dev/null || true
killall Mozc 2>/dev/null || true

rm -rf "$TARGET_DIR/Mozc.app"
cp -R "$REPO_ROOT/src/bazel-bin/mac/Mozc.app" "$TARGET_DIR/"

# 6. Install GRU model files to Application Support
APP_SUPPORT_DIR="$HOME/Library/Application Support/Google/JapaneseInput"
mkdir -p "$APP_SUPPORT_DIR"

if [ -f "$REPO_ROOT/models/grumodel.bin" ] && [ -f "$REPO_ROOT/models/chardata.bin" ]; then
  echo "[+] Copying GRU models to $APP_SUPPORT_DIR ..."
  cp -f "$REPO_ROOT/models/grumodel.bin" "$APP_SUPPORT_DIR/"
  cp -f "$REPO_ROOT/models/chardata.bin" "$APP_SUPPORT_DIR/"
fi

echo "=== Installation Complete! ==="
echo "Please follow these steps to enable Mozc:"
echo "1. Open 'System Settings' -> 'Keyboard' -> 'Text Input' -> 'Input Sources'."
echo "2. Click '+' and add 'Japanese (Mozc)'."
echo "3. If Mozc does not appear, try logging out and logging back in."
