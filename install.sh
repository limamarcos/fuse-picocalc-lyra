#!/bin/bash
set -e

echo "============================================="
echo " Installing Fuse ZX Spectrum on PicoCalc Lyra "
echo "============================================="

# Check root permissions
if [ "$(id -u)" -ne 0 ]; then
    echo "[*] Re-running installer with sudo..."
    exec sudo bash "$0" "$@"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_USER="${SUDO_USER:-pico}"
TARGET_HOME=$(getent passwd "$TARGET_USER" | cut -d: -f6)

echo "[*] Installing zero-whine sound driver (picocalc_snd_clean)..."
KVER=$(uname -r)
MOD_DIR="/lib/modules/$KVER/extra"
mkdir -p "$MOD_DIR"
rm -f "$MOD_DIR/picocalc_snd_pwm.ko" "$MOD_DIR/picocalc_snd_softpwm.ko" "$MOD_DIR/picocalc_snd_dsm.ko" 2>/dev/null || true
cp "$SCRIPT_DIR/modules/picocalc_snd_clean.ko" "$MOD_DIR/"
ln -sf picocalc_snd_clean.ko "$MOD_DIR/picocalc_snd_pwm.ko"
ln -sf picocalc_snd_clean.ko "$MOD_DIR/picocalc_snd_softpwm.ko"
depmod -a

# Blacklist old noisy drivers & configure auto-load
cp "$SCRIPT_DIR/config/sound.conf" /etc/modprobe.d/sound.conf
grep -q 'picocalc_snd_clean' /etc/modules 2>/dev/null || echo 'picocalc_snd_clean' >> /etc/modules

# Reload sound driver
modprobe -r picocalc_snd_softpwm picocalc_snd_pwm 2>/dev/null || true
modprobe picocalc_snd_clean 2>/dev/null || true

# Setup ALSA routing
echo "[*] Configuring ALSA sound routing..."
cp "$SCRIPT_DIR/config/asound.conf" /etc/asound.conf
chmod 644 /etc/asound.conf
chmod -R 666 /dev/snd/* 2>/dev/null || true

# Install Fuse binary & assets
echo "[*] Installing Fuse emulator binaries & ROMs..."
mkdir -p "$TARGET_HOME/apps/fuse/bin" "$TARGET_HOME/apps/fuse/share"
cp "$SCRIPT_DIR/bin/fuse" "$TARGET_HOME/apps/fuse/bin/"
cp -r "$SCRIPT_DIR/share/fuse" "$TARGET_HOME/apps/fuse/share/"
chmod +x "$TARGET_HOME/apps/fuse/bin/fuse"

# Install launcher to /usr/bin/fuse
cp "$SCRIPT_DIR/fuse-launcher.sh" /usr/bin/fuse
chmod +x /usr/bin/fuse

# Setup user config
echo "[*] Setting up Fuse user configuration..."
mkdir -p "$TARGET_HOME/.config/fuse-emulator" "$TARGET_HOME/games"
cp "$SCRIPT_DIR/config/fuserc" "$TARGET_HOME/.config/fuse-emulator/fuserc"
cp "$SCRIPT_DIR/config/fuserc" "$TARGET_HOME/.fuserc"

# Fix ownership
chown -R "$TARGET_USER:$TARGET_USER" "$TARGET_HOME/apps" "$TARGET_HOME/.config/fuse-emulator" "$TARGET_HOME/.fuserc" "$TARGET_HOME/games"

echo ""
echo "============================================="
echo " Installation Complete!                       "
echo " Run: fuse \"game.z80\" from terminal or launcher"
echo "============================================="
