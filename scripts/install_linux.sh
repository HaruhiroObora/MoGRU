#!/usr/bin/env bash
# ==============================================================================
# MoGRU (fcitx5-mogru) Build & Install Script for Linux
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== MoGRU (Fcitx5) Installer for Linux ==="
echo "Repository Root: $REPO_ROOT"

PREBUILT_SO=""
if [ $# -ge 1 ]; then
  if [ "$1" = "--binary" ] && [ $# -ge 2 ]; then
    PREBUILT_SO="$2"
  elif [ -f "$1" ]; then
    PREBUILT_SO="$1"
  fi
fi

if [ -n "$PREBUILT_SO" ]; then
  if [ ! -f "$PREBUILT_SO" ]; then
    echo "[!] Prebuilt binary not found: $PREBUILT_SO"
    exit 1
  fi
  PREBUILT_SO="$(cd "$(dirname "$PREBUILT_SO")" && pwd)/$(basename "$PREBUILT_SO")"
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

# 2. Check Fcitx5 development headers / runtime packages
if [ -z "$PREBUILT_SO" ]; then
  if command -v apt-get &>/dev/null; then
    MISSING_PKGS=()
    for pkg in fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev pkg-config; do
      if ! dpkg -s "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
      fi
    done
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

# 3. Build or resolve fcitx5-mogru.so
SOURCE_SO=""
if [ -z "$PREBUILT_SO" ]; then
  echo "=== Building fcitx5-mogru.so (-c opt) ==="
  cd "$REPO_ROOT/src"
  "$BAZEL_BIN" build -c opt //unix/fcitx5:fcitx5-mogru.so --//unix/fcitx5:use_server=False
  SOURCE_SO="$REPO_ROOT/src/bazel-bin/unix/fcitx5/fcitx5-mogru.so"
else
  SOURCE_SO="$PREBUILT_SO"
fi

# 4. Install plugin and addon definitions
echo "=== Installing MoGRU to ~/.local ==="
mkdir -p "$HOME/.local/lib/fcitx5"
mkdir -p "$HOME/.local/lib64/fcitx5"
mkdir -p "$HOME/.local/share/fcitx5/addon"
mkdir -p "$HOME/.local/share/fcitx5/inputmethod"
mkdir -p "$HOME/.config/mozc"

# Clean up older mozc-gru references to prevent duplicates
rm -f "$HOME/.local/lib/fcitx5/fcitx5-mozc-gru.so"
rm -f "$HOME/.local/lib64/fcitx5/fcitx5-mozc-gru.so"
rm -f "$HOME/.local/share/fcitx5/addon/mozc-gru.conf"
rm -f "$HOME/.local/share/fcitx5/inputmethod/mozc-gru.conf"

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

# 5. Copy GRU model weights and vocabulary to ~/.config/mozc/ and ~/.mozc/
if [ -f "$REPO_ROOT/models/grumodel.bin" ] && [ -f "$REPO_ROOT/models/chardata.bin" ]; then
  echo "[+] Copying default model files to ~/.config/mozc/ and ~/.mozc/ ..."
  mkdir -p "$HOME/.mozc"
  cp -f "$REPO_ROOT/models/grumodel.bin" "$HOME/.config/mozc/"
  cp -f "$REPO_ROOT/models/chardata.bin" "$HOME/.config/mozc/"
  cp -f "$REPO_ROOT/models/grumodel.bin" "$HOME/.mozc/"
  cp -f "$REPO_ROOT/models/chardata.bin" "$HOME/.mozc/"
fi

# Deploy default gru_config.ini if not already existing or upgrade legacy format
for conf_dir in "$HOME/.config/mozc" "$HOME/.mozc"; do
  mkdir -p "$conf_dir"
  TARGET_INI="$conf_dir/gru_config.ini"
  if [ ! -f "$TARGET_INI" ]; then
    if [ -f "$REPO_ROOT/models/gru_config.ini" ]; then
      echo "[+] Initializing $TARGET_INI from default template ..."
      cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
    fi
  else
    # Check if existing ini is legacy format or lacks hiragana_length_power
    if grep -q "katakana_penalty" "$TARGET_INI" || ! grep -q "hiragana_length_power" "$TARGET_INI"; then
      echo "[+] Upgrading $TARGET_INI to new format with hiragana length scaling (backup saved to $TARGET_INI.bak) ..."
      cp "$TARGET_INI" "$TARGET_INI.bak"
      cp "$REPO_ROOT/models/gru_config.ini" "$TARGET_INI"
    else
      echo "[*] Preserving existing configuration: $TARGET_INI"
    fi
  fi
done

# 6. Configure default Fcitx5 settings for MoGRU
mkdir -p "$HOME/.config/fcitx5/conf"
CONF_FILE="$HOME/.config/fcitx5/conf/mogru.conf"
DEFAULT_MODEL="$HOME/.config/mozc/grumodel.bin"
DEFAULT_CHAR="$HOME/.config/mozc/chardata.bin"

# Migrate old config if present
if [ ! -f "$CONF_FILE" ] && [ -f "$HOME/.config/fcitx5/conf/mozc-gru.conf" ]; then
  cp -f "$HOME/.config/fcitx5/conf/mozc-gru.conf" "$CONF_FILE"
fi

if [ ! -f "$CONF_FILE" ]; then
  echo "[+] Generating initial Fcitx5 configuration for MoGRU ..."
  cat << EOF > "$CONF_FILE"
# GRU Model Path (grumodel.bin)
GruModelPath=$DEFAULT_MODEL
# CharIndex Data Path (chardata.bin)
GruChardataPath=$DEFAULT_CHAR
# GRU Score Weight (Alpha)
GruScoreWeight=800
# Show GRU Score in candidate description
ShowGruScore=True
# Enable Live Conversion
EnableLiveConversion=True
EOF
else
  # If GruModelPath or GruChardataPath is empty in existing config, fill with default values
  if grep -q "^GruModelPath=$" "$CONF_FILE" || ! grep -q "^GruModelPath=" "$CONF_FILE"; then
    echo "[+] Updating empty GruModelPath in $CONF_FILE ..."
    sed -i "s|^GruModelPath=.*|GruModelPath=$DEFAULT_MODEL|" "$CONF_FILE" 2>/dev/null || true
    if ! grep -q "^GruModelPath=" "$CONF_FILE"; then
      echo "GruModelPath=$DEFAULT_MODEL" >> "$CONF_FILE"
    fi
  fi
  if grep -q "^GruChardataPath=$" "$CONF_FILE" || ! grep -q "^GruChardataPath=" "$CONF_FILE"; then
    echo "[+] Updating empty GruChardataPath in $CONF_FILE ..."
    sed -i "s|^GruChardataPath=.*|GruChardataPath=$DEFAULT_CHAR|" "$CONF_FILE" 2>/dev/null || true
    if ! grep -q "^GruChardataPath=" "$CONF_FILE"; then
      echo "GruChardataPath=$DEFAULT_CHAR" >> "$CONF_FILE"
    fi
  fi
  if ! grep -q "^ShowGruScore=" "$CONF_FILE"; then
    echo "[+] Adding ShowGruScore=True in $CONF_FILE ..."
    echo "ShowGruScore=True" >> "$CONF_FILE"
  fi
  if ! grep -q "^EnableLiveConversion=" "$CONF_FILE"; then
    echo "[+] Adding EnableLiveConversion=True in $CONF_FILE ..."
    echo "EnableLiveConversion=True" >> "$CONF_FILE"
  fi
fi

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
echo "Model paths and Alpha weight can be customized via Fcitx5 GUI Settings."
