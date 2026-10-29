#!/usr/bin/env bash
# ==============================================================================
# MoGRU (fcitx5-mogru) Build & Install Script for Linux
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== MoGRU (Fcitx5) Installer for Linux ==="
echo "Repository Root: $REPO_ROOT"

PREBUILT_ARG=""
if [ $# -ge 1 ]; then
  if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo "Usage: $0 [ [--binary] <path-to-fcitx5-mogru.so-or-zip> ]"
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

PREBUILT_SO=""
TEMP_EXTRACT_DIR=""
cleanup() {
  if [ -n "$TEMP_EXTRACT_DIR" ] && [ -d "$TEMP_EXTRACT_DIR" ]; then
    rm -rf "$TEMP_EXTRACT_DIR"
  fi
}
trap cleanup EXIT

if [ -n "$PREBUILT_ARG" ]; then
  if [ ! -e "$PREBUILT_ARG" ]; then
    echo "[!] Prebuilt binary not found: $PREBUILT_ARG"
    exit 1
  fi
  if [ -f "$PREBUILT_ARG" ]; then
    ABS_FILE="$(cd "$(dirname "$PREBUILT_ARG")" && pwd)/$(basename "$PREBUILT_ARG")"
    if [[ "$ABS_FILE" == *.zip ]]; then
      TEMP_EXTRACT_DIR="$(mktemp -d)"
      echo "[+] Extracting CI artifact archive: $ABS_FILE"
      unzip -q "$ABS_FILE" -d "$TEMP_EXTRACT_DIR"
      FOUND_SO="$(find "$TEMP_EXTRACT_DIR" -name "*.so" -print -quit 2>/dev/null || true)"
      if [ -n "$FOUND_SO" ]; then
        PREBUILT_SO="$FOUND_SO"
      else
        echo "[!] No .so binary found inside archive: $ABS_FILE"
        exit 1
      fi
    else
      PREBUILT_SO="$ABS_FILE"
    fi
  elif [ -d "$PREBUILT_ARG" ]; then
    ABS_DIR="$(cd "$PREBUILT_ARG" && pwd)"
    FOUND_SO="$(find "$ABS_DIR" -maxdepth 2 -name "*.so" -print -quit 2>/dev/null || true)"
    if [ -n "$FOUND_SO" ]; then
      PREBUILT_SO="$FOUND_SO"
    else
      echo "[!] No .so binary found inside directory: $ABS_DIR"
      exit 1
    fi
  fi
  echo "[+] Using prebuilt binary from CI/Release: $PREBUILT_SO"
  echo "    Skipping Bazel build step."
else
  # 1. Check Bazel / Bazelisk
  export USE_BAZEL_VERSION="${USE_BAZEL_VERSION:-9.0.2}"
  BAZEL_BIN=""
  if command -v bazelisk &>/dev/null; then
    BAZEL_BIN="bazelisk"
  elif command -v bazel &>/dev/null; then
    BAZEL_BIN="bazel"
  else
    echo "[!] Bazel or Bazelisk was not found."
    echo "    Attempting to download Bazelisk to ~/.local/bin/bazel ..."
    mkdir -p "$HOME/.local/bin"
    curl -fsSL -o "$HOME/.local/bin/bazel" \
      https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
    chmod +x "$HOME/.local/bin/bazel"
    export PATH="$HOME/.local/bin:$PATH"
    BAZEL_BIN="$HOME/.local/bin/bazel"
  fi
  echo "[+] Using Bazel: $("$BAZEL_BIN" --version 2>/dev/null || echo "$BAZEL_BIN")"
fi

# 2. Check Fcitx5 development headers / runtime packages and Qt
if [ -z "$PREBUILT_SO" ]; then
  if command -v apt-get &>/dev/null; then
    MISSING_PKGS=()
    for pkg in fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev pkg-config; do
      if ! dpkg -s "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
      fi
    done
    # Check Qt (Qt6 preferred, fallback to Qt5) for mozc_tool GUI config dialog
    if ! dpkg -s qt6-base-dev &>/dev/null && ! dpkg -s qtbase5-dev &>/dev/null; then
      MISSING_PKGS+=("qt6-base-dev")
    fi
    if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
      echo "[!] Missing required packages: ${MISSING_PKGS[*]}"
      echo "    Please install them with: sudo apt install -y ${MISSING_PKGS[*]}"
      read -rp "Run sudo apt install now? [y/N]: " ans
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        sudo apt update && sudo apt install -y "${MISSING_PKGS[@]}"
      fi
    fi
  elif command -v dnf &>/dev/null; then
    MISSING_PKGS=()
    for pkg in fcitx5 fcitx5-devel pkgconf-pkg-config gcc-c++; do
      if ! rpm -q "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
      fi
    done
    if ! rpm -q qt6-qtbase-devel &>/dev/null && ! rpm -q qt5-qtbase-devel &>/dev/null; then
      MISSING_PKGS+=("qt6-qtbase-devel")
    fi
    if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
      echo "[!] Missing required RedHat/Fedora packages: ${MISSING_PKGS[*]}"
      echo "    Please install them with: sudo dnf install -y ${MISSING_PKGS[*]}"
      read -rp "Run sudo dnf install now? [y/N]: " ans
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        sudo dnf install -y "${MISSING_PKGS[@]}"
      fi
    fi
  elif command -v yum &>/dev/null; then
    MISSING_PKGS=()
    for pkg in fcitx5 fcitx5-devel pkgconf-pkg-config gcc-c++; do
      if ! rpm -q "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
      fi
    done
    if ! rpm -q qt6-qtbase-devel &>/dev/null && ! rpm -q qt5-qtbase-devel &>/dev/null; then
      MISSING_PKGS+=("qt6-qtbase-devel")
    fi
    if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
      echo "[!] Missing required RedHat/CentOS packages: ${MISSING_PKGS[*]}"
      echo "    Please install them with: sudo yum install -y ${MISSING_PKGS[*]}"
      read -rp "Run sudo yum install now? [y/N]: " ans
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        sudo yum install -y "${MISSING_PKGS[@]}"
      fi
    fi
  elif command -v pacman &>/dev/null; then
    MISSING_PKGS=()
    for pkg in fcitx5 fcitx5-im base-devel; do
      if ! pacman -Q "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
      fi
    done
    if ! pacman -Q qt6-base &>/dev/null && ! pacman -Q qt5-base &>/dev/null; then
      MISSING_PKGS+=("qt6-base")
    fi
    if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
      echo "[!] Missing required Arch packages: ${MISSING_PKGS[*]}"
      echo "    Please install them with: sudo pacman -S --needed ${MISSING_PKGS[*]}"
      read -rp "Run sudo pacman -S now? [y/N]: " ans
      if [[ "$ans" =~ ^[Yy]$ ]]; then
        sudo pacman -S --needed "${MISSING_PKGS[@]}"
      fi
    fi
  fi
else
  # Prebuilt binary only requires fcitx5 runtime
  if ! command -v fcitx5 &>/dev/null; then
    echo "[!] fcitx5 runtime not found."
    if command -v apt-get &>/dev/null; then
      echo "    Please install Fcitx5 with: sudo apt install -y fcitx5"
    elif command -v dnf &>/dev/null; then
      echo "    Please install Fcitx5 with: sudo dnf install -y fcitx5"
    elif command -v pacman &>/dev/null; then
      echo "    Please install Fcitx5 with: sudo pacman -S fcitx5 fcitx5-im"
    fi
  fi
fi

# 3. Build or resolve fcitx5-mogru.so (and mozc_tool for GUI ConfigDialog)
SOURCE_SO=""
SOURCE_MOZC_TOOL=""
if [ -z "$PREBUILT_SO" ]; then
  echo "=== Building fcitx5-mogru.so (-c opt) ==="
  cd "$REPO_ROOT/src"
  "$BAZEL_BIN" build -c opt //unix/fcitx5:fcitx5-mogru.so --//unix/fcitx5:use_server=False
  SOURCE_SO="$REPO_ROOT/src/bazel-bin/unix/fcitx5/fcitx5-mogru.so"

  # Build mozc_tool only if Qt headers are available, avoiding loud Bazel repository fetch errors
  if pkg-config --exists Qt6Core Qt6Gui Qt6Widgets 2>/dev/null || pkg-config --exists Qt5Core Qt5Gui Qt5Widgets 2>/dev/null; then
    echo "=== Building mozc_tool GUI Config Dialog (-c opt) ==="
    if "$BAZEL_BIN" build -c opt //gui/tool:mozc_tool --//unix/fcitx5:use_server=False; then
      SOURCE_MOZC_TOOL="$REPO_ROOT/src/bazel-bin/gui/tool/mozc_tool"
    else
      echo "[*] Optional //gui/tool:mozc_tool build failed."
    fi
  else
    echo "[*] Qt development libraries (Qt6/Qt5) not found. Skipping optional //gui/tool:mozc_tool build."
    echo "    (Note: fcitx5-mogru.so was built successfully. Install qt6-base-dev if you want the GUI config dialog.)"
  fi
else
  SOURCE_SO="$PREBUILT_SO"
fi

# 4. Install plugin and addon definitions
echo "=== Installing MoGRU to ~/.local ==="
mkdir -p "$HOME/.local/lib/fcitx5"
mkdir -p "$HOME/.local/lib64/fcitx5"
mkdir -p "$HOME/.local/lib/mogru"
mkdir -p "$HOME/.local/share/fcitx5/addon"
mkdir -p "$HOME/.local/share/fcitx5/inputmethod"
mkdir -p "$HOME/.config/mogru"

if [ -n "$SOURCE_MOZC_TOOL" ] && [ -f "$SOURCE_MOZC_TOOL" ]; then
  cp -f "$SOURCE_MOZC_TOOL" "$HOME/.local/lib/mogru/mozc_tool"
  chmod 755 "$HOME/.local/lib/mogru/mozc_tool"
  ln -sf "$HOME/.local/lib/mogru/mozc_tool" "$HOME/.local/lib/mogru/mogru_tool"
  echo "[+] Installed MoGRU Qt GUI tool to $HOME/.local/lib/mogru/mozc_tool"
fi

# Clean up older mozc-gru references to prevent duplicates
rm -f "$HOME/.local/lib/fcitx5/fcitx5-mozc-gru.so"
rm -f "$HOME/.local/lib64/fcitx5/fcitx5-mozc-gru.so"
rm -f "$HOME/.local/share/fcitx5/addon/mozc-gru.conf"
rm -f "$HOME/.local/share/fcitx5/inputmethod/mozc-gru.conf"

# Clean up older MoGRU tool from ~/.local/lib/mozc/ if present
if [ -f "$HOME/.local/lib/mozc/mozc_tool" ]; then
  echo "[+] Cleaning up legacy MoGRU tool from ~/.local/lib/mozc/mozc_tool ..."
  rm -f "$HOME/.local/lib/mozc/mozc_tool"
  rmdir "$HOME/.local/lib/mozc" 2>/dev/null || true
fi

# Copy library (install to both lib and lib64 for multi-distro compatibility)
rm -f "$HOME/.local/lib/fcitx5/fcitx5-mogru.so"
rm -f "$HOME/.local/lib64/fcitx5/fcitx5-mogru.so"
cp -f "$SOURCE_SO" "$HOME/.local/lib/fcitx5/fcitx5-mogru.so"
cp -f "$SOURCE_SO" "$HOME/.local/lib64/fcitx5/fcitx5-mogru.so"
chmod 755 "$HOME/.local/lib/fcitx5/fcitx5-mogru.so" "$HOME/.local/lib64/fcitx5/fcitx5-mogru.so"

# Create / copy addon conf
cat << 'EOF' > "$HOME/.local/share/fcitx5/addon/mogru.conf"
[Addon]
Name=MoGRU
Category=InputMethod
Library=fcitx5-mogru
Type=SharedLibrary
OnDemand=True
Configurable=True

[Addon/OptionalDependencies]
0=clipboard
EOF

# Create / copy input method conf
cat << 'EOF' > "$HOME/.local/share/fcitx5/inputmethod/mogru.conf"
[InputMethod]
Name=MoGRU
Icon=fcitx_mozc
Label=あ(M)
LangCode=ja
Addon=mogru
Configurable=True
EOF

# 5. Migrate old MoGRU configuration and learned model from ~/.config/mozc if present
if [ -d "$HOME/.config/mozc" ] || [ -d "$HOME/.mozc" ]; then
  for legacy_dir in "$HOME/.config/mozc" "$HOME/.mozc"; do
    if [ -d "$legacy_dir" ]; then
      if [ -f "$legacy_dir/gru_config.ini" ] && [ ! -f "$HOME/.config/mogru/gru_config.ini" ]; then
        echo "[+] Migrating existing gru_config.ini from $legacy_dir to ~/.config/mogru/ ..."
        cp -p "$legacy_dir/gru_config.ini" "$HOME/.config/mogru/gru_config.ini"
      fi
      if [ -f "$legacy_dir/grumodel_user.bin" ] && [ ! -f "$HOME/.config/mogru/grumodel_user.bin" ]; then
        echo "[+] Migrating existing grumodel_user.bin from $legacy_dir to ~/.config/mogru/ ..."
        cp -p "$legacy_dir/grumodel_user.bin" "$HOME/.config/mogru/grumodel_user.bin"
      fi
    fi
  done

  # Clean up legacy MoGRU files from mozc directories to remove pollution
  echo "[+] Cleaning up legacy MoGRU files from ~/.config/mozc/ and ~/.mozc/ ..."
  for legacy_dir in "$HOME/.config/mozc" "$HOME/.mozc"; do
    if [ -d "$legacy_dir" ]; then
      rm -f "$legacy_dir/grumodel.bin"
      rm -f "$legacy_dir/chardata.bin"
      rm -f "$legacy_dir/gru_config.ini"
      rm -f "$legacy_dir/gru_config.ini.bak"
      rm -f "$legacy_dir/grumodel_user.bin"
    fi
  done
fi

# Copy GRU model weights and vocabulary to ~/.config/mogru/
if [ -f "$REPO_ROOT/models/grumodel.bin" ] && [ -f "$REPO_ROOT/models/chardata.bin" ]; then
  echo "[+] Copying default model files to ~/.config/mogru/ ..."
  cp -f "$REPO_ROOT/models/grumodel.bin" "$HOME/.config/mogru/"
  cp -f "$REPO_ROOT/models/chardata.bin" "$HOME/.config/mogru/"
fi

# Deploy default gru_config.ini if not already existing or upgrade legacy format
TARGET_INI="$HOME/.config/mogru/gru_config.ini"
if [ ! -f "$TARGET_INI" ]; then
  if [ -f "$REPO_ROOT/models/gru_config.ini" ]; then
    echo "[+] Initializing $TARGET_INI from default template ..."
    cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
  fi
else
  # Check if existing ini is legacy format or lacks enable_live_conversion
  if grep -q "katakana_penalty" "$TARGET_INI" || ! grep -q "enable_live_conversion" "$TARGET_INI"; then
    echo "[+] Upgrading $TARGET_INI to unified MoGRU config format (backup saved to $TARGET_INI.bak) ..."
    cp "$TARGET_INI" "$TARGET_INI.bak"
    cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
  else
    echo "[*] Preserving existing configuration: $TARGET_INI"
  fi
fi

# 6. Configure Fcitx5 directory
mkdir -p "$HOME/.config/fcitx5/conf"

# 7. Restart Fcitx5
echo "=== Restarting Fcitx5 ==="
if command -v fcitx5 &>/dev/null; then
  # Include standard Debian/Ubuntu, RedHat/Fedora/openSUSE, and Arch addon locations
  FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:$HOME/.local/lib64/fcitx5:/usr/lib64/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5:/usr/lib/fcitx5" fcitx5 -r -d >/dev/null 2>&1 </dev/null || true
  echo "[+] Fcitx5 restarted successfully."
else
  echo "[!] fcitx5 command not found in PATH. Please restart Fcitx5 manually."
fi

echo "=== Installation Complete! ==="
echo "You can now add 'MoGRU' in Fcitx5 Configuration (fcitx5-config-qt)."
echo "MoGRU settings can be customized via 'Configuration Tool' (mozc_tool) or ~/.config/mogru/gru_config.ini."
