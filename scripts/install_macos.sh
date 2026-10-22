#!/usr/bin/env bash
# ==============================================================================
# MoGRU Build & Install Script for macOS
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== MoGRU Installer for macOS ==="
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

# 3. Check CMake (required to build Qt)
if ! command -v cmake &>/dev/null; then
  if command -v brew &>/dev/null; then
    echo "[!] Installing CMake via Homebrew..."
    brew install cmake
  else
    echo "[Error] CMake not found. Please install CMake via 'brew install cmake'."
    exit 1
  fi
fi

# 4. Check Bazel / Bazelisk
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

cd "$REPO_ROOT/src"

# 5. Build Qt if not already present
if [ ! -d "$REPO_ROOT/src/third_party/qt" ] && [ -z "${MOZC_QT_PATH:-}" ]; then
  echo "=== Step 1/3: Downloading dependencies & Building Qt ==="
  echo "Note: Building Qt may take several minutes on the first run."
  python3 build_tools/update_deps.py
  python3 build_tools/build_qt.py --release --confirm_license
fi

# 6. Build Mozc package for macOS
echo "=== Step 2/3: Building Mozc Package (-c opt) ==="
"$BAZEL_BIN" build package --config release_build || {
  echo "[!] Package build failed, falling back to //mac:mozc_macos..."
  "$BAZEL_BIN" build //mac:mozc_macos --config release_build
}

# 7. Install
echo "=== Step 3/3: Installing MoGRU ==="

# Copy GRU model files and default configuration to user profile directory
for dir in \
  "$HOME/Library/Application Support/Mozc" \
  "$HOME/Library/Application Support/Google/JapaneseInput" \
  "$HOME/.config/mozc"; do
  mkdir -p "$dir"
  if [ -f "$REPO_ROOT/models/grumodel.bin" ] && [ -f "$REPO_ROOT/models/chardata.bin" ]; then
    cp -f "$REPO_ROOT/models/grumodel.bin" "$dir/"
    cp -f "$REPO_ROOT/models/chardata.bin" "$dir/"
  fi
  TARGET_INI="$dir/gru_config.ini"
  if [ ! -f "$TARGET_INI" ] && [ -f "$REPO_ROOT/models/gru_config.ini" ]; then
    echo "[+] Initializing $TARGET_INI from default template ..."
    cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
  elif [ -f "$TARGET_INI" ] && grep -q "katakana_penalty" "$TARGET_INI" && ! grep -q "katakana_multiplier" "$TARGET_INI"; then
    echo "[+] Upgrading legacy $TARGET_INI to new format (backup saved to $TARGET_INI.bak) ..."
    cp "$TARGET_INI" "$TARGET_INI.bak"
    cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
  fi
done
echo "[+] GRU model files and configuration installed to Application Support and ~/.config/mozc/"

# If Mozc.pkg exists, install it
if [ -f "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg" ]; then
  echo "[+] Found Mozc.pkg installer."
  if command -v sudo &>/dev/null; then
    echo "Running installer (requires administrator password)..."
    sudo installer -pkg "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg" -target / || {
      echo "[!] sudo installer failed, opening installer window..."
      open "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg"
    }
  else
    open "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg"
  fi
elif [ -d "$REPO_ROOT/src/bazel-bin/mac/Mozc.app" ]; then
  TARGET_DIR="$HOME/Library/Input Methods"
  mkdir -p "$TARGET_DIR"
  killall MozcConverter 2>/dev/null || true
  killall MozcRenderer 2>/dev/null || true
  killall Mozc 2>/dev/null || true
  rm -rf "$TARGET_DIR/Mozc.app"
  cp -R "$REPO_ROOT/src/bazel-bin/mac/Mozc.app" "$TARGET_DIR/"
  echo "[+] Mozc.app copied to $TARGET_DIR/"
fi

echo "=== Installation Complete! ==="
echo "Please follow these steps to enable MoGRU:"
echo "1. Open 'System Settings' -> 'Keyboard' -> 'Text Input' -> 'Input Sources'."
echo "2. Click '+' and add 'Japanese (MoGRU)'."
echo "3. If MoGRU does not appear, log out and log back in."
