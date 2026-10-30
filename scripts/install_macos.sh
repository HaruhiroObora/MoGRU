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
    echo "Usage: $0 [ [--binary] <path-to-MoGRU.pkg-or-zip> ]"
    echo "       (When run inside extracted MoGRU package, arguments can be omitted)"
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
else
  # Auto-detection when run without arguments inside an extracted standalone package
  FOUND_LOCAL_PKG="$(find "$SCRIPT_DIR" -maxdepth 2 -name "*.pkg" -print -quit 2>/dev/null || true)"
  FOUND_LOCAL_APP="$(find "$SCRIPT_DIR" -maxdepth 2 -name "*.app" -type d -print -quit 2>/dev/null || true)"
  if [ -n "$FOUND_LOCAL_PKG" ]; then
    PREBUILT_ARG="$FOUND_LOCAL_PKG"
    echo "[+] Auto-detected standalone package in script directory: $FOUND_LOCAL_PKG"
  elif [ -n "$FOUND_LOCAL_APP" ]; then
    PREBUILT_ARG="$FOUND_LOCAL_APP"
    echo "[+] Auto-detected standalone app in script directory: $FOUND_LOCAL_APP"
  fi
fi

SOURCE_PKG=""
SOURCE_APP=""
PREBUILT_MODELS_DIR=""
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
      FOUND_MODELS="$(find "$TEMP_EXTRACT_DIR" -type d -name "models" -print -quit 2>/dev/null || true)"
      if [ -n "$FOUND_PKG" ]; then
        SOURCE_PKG="$FOUND_PKG"
      elif [ -n "$FOUND_APP" ]; then
        SOURCE_APP="$FOUND_APP"
      else
        echo "[!] No .pkg or .app found inside archive: $ABS_FILE"
        exit 1
      fi
      if [ -n "$FOUND_MODELS" ]; then
        PREBUILT_MODELS_DIR="$FOUND_MODELS"
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
      FOUND_MODELS="$(find "$ABS_DIR" -type d -name "models" -print -quit 2>/dev/null || true)"
      if [ -n "$FOUND_PKG" ]; then
        SOURCE_PKG="$FOUND_PKG"
      elif [ -n "$FOUND_APP" ]; then
        SOURCE_APP="$FOUND_APP"
      else
        echo "[!] No .pkg or .app found inside directory: $ABS_DIR"
        exit 1
      fi
      if [ -n "$FOUND_MODELS" ]; then
        PREBUILT_MODELS_DIR="$FOUND_MODELS"
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

  # 3. Check Python 3 & CMake (required to build Qt)
  if ! command -v python3 &>/dev/null; then
    if command -v brew &>/dev/null; then
      echo "[!] Installing python3 via Homebrew..."
      brew install python3
    else
      echo "[Error] python3 not found. Please install Python 3."
      exit 1
    fi
  fi

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

  if [ -f "$REPO_ROOT/src/bazel-bin/mac/MoGRU.pkg" ]; then
    SOURCE_PKG="$REPO_ROOT/src/bazel-bin/mac/MoGRU.pkg"
  elif [ -f "$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg" ]; then
    SOURCE_PKG="$REPO_ROOT/src/bazel-bin/mac/Mozc.pkg"
  elif [ -d "$REPO_ROOT/src/bazel-bin/mac/MoGRU.app" ]; then
    SOURCE_APP="$REPO_ROOT/src/bazel-bin/mac/MoGRU.app"
  elif [ -d "$REPO_ROOT/src/bazel-bin/mac/Mozc.app" ]; then
    SOURCE_APP="$REPO_ROOT/src/bazel-bin/mac/Mozc.app"
  fi
fi

# 7. Install
echo "=== Installing MoGRU ==="

# Copy GRU model files and default configuration to user profile directory
SOURCE_MODELS_DIR=""
if [ -d "$REPO_ROOT/models" ] && [ -f "$REPO_ROOT/models/grumodel.bin" ]; then
  SOURCE_MODELS_DIR="$REPO_ROOT/models"
elif [ -d "$SCRIPT_DIR/models" ] && [ -f "$SCRIPT_DIR/models/grumodel.bin" ]; then
  SOURCE_MODELS_DIR="$SCRIPT_DIR/models"
elif [ -n "$PREBUILT_MODELS_DIR" ] && [ -d "$PREBUILT_MODELS_DIR" ]; then
  SOURCE_MODELS_DIR="$PREBUILT_MODELS_DIR"
elif [ -d "./models" ] && [ -f "./models/grumodel.bin" ]; then
  SOURCE_MODELS_DIR="./models"
fi

for dir in \
  "$HOME/Library/Application Support/MoGRU" \
  "$HOME/.config/mogru"; do
  mkdir -p "$dir"
  if [ -n "$SOURCE_MODELS_DIR" ]; then
    echo "[+] Copying model files to $dir from $SOURCE_MODELS_DIR ..."
    for bin_file in "$SOURCE_MODELS_DIR"/*.bin; do
      if [ -f "$bin_file" ]; then
        echo "    - $(basename "$bin_file")"
        cp -f "$bin_file" "$dir/"
      fi
    done
  fi
  TARGET_INI="$dir/gru_config.ini"
  CONFIG_SRC=""
  if [ -n "$SOURCE_MODELS_DIR" ] && [ -f "$SOURCE_MODELS_DIR/gru_config.ini" ]; then
    CONFIG_SRC="$SOURCE_MODELS_DIR/gru_config.ini"
  elif [ -f "$REPO_ROOT/models/gru_config.ini" ]; then
    CONFIG_SRC="$REPO_ROOT/models/gru_config.ini"
  fi

  if [ ! -f "$TARGET_INI" ]; then
    if [ -n "$CONFIG_SRC" ]; then
      echo "[+] Initializing $TARGET_INI from default template ..."
      cp "$CONFIG_SRC" "$TARGET_INI"
    fi
  elif [ -f "$TARGET_INI" ] && (grep -q "katakana_penalty" "$TARGET_INI" || ! grep -q "enable_live_conversion" "$TARGET_INI"); then
    echo "[+] Upgrading $TARGET_INI to unified MoGRU config format (backup saved to $TARGET_INI.bak) ..."
    cp "$TARGET_INI" "$TARGET_INI.bak"
    if [ -n "$CONFIG_SRC" ]; then
      cp "$CONFIG_SRC" "$TARGET_INI"
    fi
  fi
done
echo "[+] GRU model files and configuration installed to Application Support and ~/.config/mogru/"

# Clean up legacy MoGRU files from Mozc directories to remove pollution
for legacy_dir in \
  "$HOME/Library/Application Support/Mozc" \
  "$HOME/Library/Application Support/Google/JapaneseInput" \
  "$HOME/.config/mozc"; do
  if [ -d "$legacy_dir" ]; then
    rm -f "$legacy_dir/grumodel.bin"
    rm -f "$legacy_dir/chardata.bin"
    rm -f "$legacy_dir/gru_config.ini"
    rm -f "$legacy_dir/gru_config.ini.bak"
    rm -f "$legacy_dir/grumodel_user.bin"
  fi
done

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
  killall MoGRUConverter 2>/dev/null || true
  killall MoGRURenderer 2>/dev/null || true
  killall MoGRU 2>/dev/null || true
  killall MozcConverter 2>/dev/null || true
  killall MozcRenderer 2>/dev/null || true
  killall Mozc 2>/dev/null || true
  rm -rf "$TARGET_DIR/MoGRU.app" "$TARGET_DIR/Mozc.app"
  cp -R "$SOURCE_APP" "$TARGET_DIR/MoGRU.app"
  echo "[+] MoGRU.app copied to $TARGET_DIR/"
else
  echo "[Error] Neither MoGRU.pkg/Mozc.pkg nor MoGRU.app/Mozc.app was found to install."
  exit 1
fi

echo "=== Installation Complete! ==="
echo "Please follow these steps to enable MoGRU:"
echo "1. Open 'System Settings' -> 'Keyboard' -> 'Text Input' -> 'Input Sources'."
echo "2. Click '+' and add 'Japanese (MoGRU)'."
echo "3. If MoGRU does not appear, log out and log back in."
