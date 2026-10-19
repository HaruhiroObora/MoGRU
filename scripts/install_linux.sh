#!/usr/bin/env bash
# ==============================================================================
# Mozc GRU (fcitx5-mozc-gru) Build & Install Script for Linux
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Mozc GRU (Fcitx5) Installer for Linux ==="
echo "Repository Root: $REPO_ROOT"

# 1. Check Bazel / Bazelisk
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

# 2. Check Fcitx5 development headers
if command -v apt-get &>/dev/null; then
  MISSING_PKGS=()
  for pkg in fcitx5 libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev fcitx5-modules-dev; do
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
fi

# 3. Build fcitx5-mozc-gru.so
echo "=== Building fcitx5-mozc-gru.so (-c opt) ==="
cd "$REPO_ROOT/src"
"$BAZEL_BIN" build -c opt //unix/fcitx5:fcitx5-mozc-gru.so --//unix/fcitx5:use_server=False

# 4. Install plugin and addon definitions
echo "=== Installing Mozc GRU to ~/.local ==="
mkdir -p "$HOME/.local/lib/fcitx5"
mkdir -p "$HOME/.local/share/fcitx5/addon"
mkdir -p "$HOME/.local/share/fcitx5/inputmethod"
mkdir -p "$HOME/.config/mozc"

# Copy library
rm -f "$HOME/.local/lib/fcitx5/fcitx5-mozc-gru.so"
cp -f "$REPO_ROOT/src/bazel-bin/unix/fcitx5/fcitx5-mozc-gru.so" "$HOME/.local/lib/fcitx5/"
chmod 755 "$HOME/.local/lib/fcitx5/fcitx5-mozc-gru.so"

# Create / copy addon conf
cat << 'EOF' > "$HOME/.local/share/fcitx5/addon/mozc-gru.conf"
[Addon]
Name=Mozc (GRU)
Category=InputMethod
Library=fcitx5-mozc-gru
Type=SharedLibrary
OnDemand=True
Configurable=True

[Addon/OptionalDependencies]
0=clipboard
EOF

# Create / copy input method conf
cat << 'EOF' > "$HOME/.local/share/fcitx5/inputmethod/mozc-gru.conf"
[InputMethod]
Name=Mozc (GRU)
Icon=fcitx_mozc
Label=あ(G)
LangCode=ja
Addon=mozc-gru
Configurable=True
EOF

# 5. Copy GRU model weights and vocabulary to ~/.config/mozc/
if [ -f "$REPO_ROOT/models/grumodel.bin" ] && [ -f "$REPO_ROOT/models/chardata.bin" ]; then
  echo "[+] Copying default model files to ~/.config/mozc/ ..."
  cp -f "$REPO_ROOT/models/grumodel.bin" "$HOME/.config/mozc/"
  cp -f "$REPO_ROOT/models/chardata.bin" "$HOME/.config/mozc/"
fi

# 6. Restart Fcitx5
echo "=== Restarting Fcitx5 ==="
if command -v fcitx5 &>/dev/null; then
  FCITX_ADDON_DIRS="$HOME/.local/lib/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5" fcitx5 -r -d || true
  echo "[+] Fcitx5 restarted successfully."
else
  echo "[!] fcitx5 command not found in PATH. Please restart Fcitx5 manually."
fi

echo "=== Installation Complete! ==="
echo "You can now add 'Mozc (GRU)' in Fcitx5 Configuration (fcitx5-config-qt)."
echo "Model paths and Alpha weight can be customized via Fcitx5 GUI Settings."
