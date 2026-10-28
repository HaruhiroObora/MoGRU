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

PREBUILT_ARG=""
if [ $# -ge 1 ]; then
  if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "Usage: $0 [ [--binary] <path-to-Mozc.pkg-or-zip> ]"
    exit 0
  elif [ "$1" = "--binary" ]; then
    if [ $# -lt 2 ]; then
      echo "[!] Missing argument for --binary"
      exit 1
    fi
    PREBUILT_ARG="$2"
  else
    PREBUILT_ARG="$1"
  fi
fi

SOURCE_PKG=""
SOURCE_APP=""
TEMP_EXTRACT_DIR=""
cleanup() {
  if [ -n "$TEMP_EXTRACT_DIR" ] && [ -d "$TEMP_EXTRACT_DIR" ]; then
    rm -rf "$TEMP_EXTRACT_DIR"
  fi
}
trap cleanup EXIT

if [ -n "$PREBUILT_ARG" ]; then
  if [ ! -e "$PREBUILT_ARG" ]; then
    echo "[!] Prebuilt package not found: $PREBUILT_ARG"
    exit 1
  fi

  if [ -f "$PREBUILT_ARG" ]; then
    ABS_FILE="$(cd "$(dirname "$PREBUILT_ARG")" && pwd)/$(basename "$PREBUILT_ARG")"
    if [[ "$ABS_FILE" == *.zip ]]; then
      TEMP_EXTRACT_DIR="$(mktemp -d)"
      echo "[+] Extracting CI artifact archive: $ABS_FILE"
      unzip -q "$ABS_FILE" -d "$TEMP_EXTRACT_DIR"
      FOUND_PKG="$(find "$TEMP_EXTRACT_DIR" -name "*.pkg" -print -quit 2>/dev/null || true)"
      FOUND_APP="$(find "$TEMP_EXTRACT_DIR" -name "*.app" -type d -print -quit 2>/dev/null || true)"
      if [ -n "$FOUND_PKG" ]; then
        SOURCE_PKG="$FOUND_PKG"
      elif [ -n "$FOUND_APP" ]; then
        SOURCE_APP="$FOUND_APP"
      else
        echo "[!] No .pkg or .app found inside archive: $ABS_FILE"
        exit 1
      fi
    else
      SOURCE_PKG="$ABS_FILE"
    fi
  elif [ -d "$PREBUILT_ARG" ]; then
    ABS_DIR="$(cd "$PREBUILT_ARG" && pwd)"
    if [[ "$ABS_DIR" == *.app ]]; then
      SOURCE_APP="$ABS_DIR"
    elif [[ "$ABS_DIR" == *.pkg ]] && [ -d "$ABS_DIR/Contents" ]; then
      SOURCE_PKG="$ABS_DIR"
    else
      FOUND_PKG="$(find "$ABS_DIR" -maxdepth 2 -name "*.pkg" -print -quit 2>/dev/null || true)"
      FOUND_APP="$(find "$ABS_DIR" -maxdepth 2 -name "*.app" -type d -print -quit 2>/dev/null || true)"
      if [ -n "$FOUND_PKG" ]; then
        SOURCE_PKG="$FOUND_PKG"
      elif [ -n "$FOUND_APP" ]; then
        SOURCE_APP="$FOUND_APP"
      else
        echo "[!] No .pkg or .app found inside directory: $ABS_DIR"
        exit 1
      fi
    fi
  fi

  echo "[+] Using prebuilt artifact from CI/Release: ${SOURCE_PKG:-$SOURCE_APP}"
  echo "    Skipping Xcode/CMake/Qt/Bazel build steps."
else
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

  if [ -f "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg" ]; then
    SOURCE_PKG="$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg"
  elif [ -d "$REPO_ROOT/src/bazel-bin/mac/Mozc.app" ]; then
    SOURCE_APP="$REPO_ROOT/src/bazel-bin/mac/Mozc.app"
  fi
fi

# 7. Install
echo "=== Installing MoGRU ==="

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
  elif [ -f "$TARGET_INI" ] && (grep -q "katakana_penalty" "$TARGET_INI" || ! grep -q "enable_live_conversion" "$TARGET_INI"); then
    echo "[+] Upgrading $TARGET_INI to unified MoGRU config format (backup saved to $TARGET_INI.bak) ..."
    cp "$TARGET_INI" "$TARGET_INI.bak"
    cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
  fi
done
echo "[+] GRU model files and configuration installed to Application Support and ~/.config/mozc/"

# If Mozc.pkg exists, install it
if [ -n "$SOURCE_PKG" ] && [ -e "$SOURCE_PKG" ]; then
  echo "[+] Installing package: $SOURCE_PKG"
  if command -v sudo &>/dev/null; then
    echo "Running installer (requires administrator password)..."
    sudo installer -pkg "$SOURCE_PKG" -target / || {
      echo "[!] sudo installer failed, opening installer window..."
      open -W "$SOURCE_PKG"
    }
  else
    open -W "$SOURCE_PKG"
  fi
elif [ -n "$SOURCE_APP" ] && [ -d "$SOURCE_APP" ]; then
  TARGET_DIR="$HOME/Library/Input Methods"
  mkdir -p "$TARGET_DIR"
  killall MozcConverter 2>/dev/null || true
  killall MozcRenderer 2>/dev/null || true
  killall Mozc 2>/dev/null || true
  rm -rf "$TARGET_DIR/Mozc.app"
  cp -R "$SOURCE_APP" "$TARGET_DIR/Mozc.app"
  echo "[+] Mozc.app copied to $TARGET_DIR/"
else
  echo "[Error] Neither Mozc.pkg nor Mozc.app was found to install."
  exit 1
fi

echo "=== Installation Complete! ==="
echo "Please follow these steps to enable MoGRU:"
echo "1. Open 'System Settings' -> 'Keyboard' -> 'Text Input' -> 'Input Sources'."
echo "2. Click '+' and add 'Japanese (MoGRU)'."
echo "3. If MoGRU does not appear, log out and log back in."
