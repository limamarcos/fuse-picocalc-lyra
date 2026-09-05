#!/bin/bash
# PicoCalc launcher for Fuse ZX Spectrum emulator
export HOME=/home/pico
GAMES=/home/pico/games

# Unblank screen and disable console screensaver during emulation
echo 0 > /sys/class/graphics/fb0/blank 2>/dev/null || true
echo -ne "\033[9;0]\033[14;0]" > /dev/tty1 2>/dev/null || true

args=()
for a in "$@"; do
  if [[ $a == -* ]]; then
    args+=("$a")
  elif [ -e "$a" ]; then
    # File exists in caller's current working directory
    args+=("$(realpath "$a")")
  elif [ -e "$GAMES/$a" ]; then
    # File exists in /home/pico/games/
    args+=("$GAMES/$a")
  elif [ -e "$HOME/$a" ]; then
    # File exists in /home/pico/
    args+=("$HOME/$a")
  else
    # Case-insensitive match in /home/pico/games/
    matched=$(find "$GAMES" -maxdepth 1 -iname "$a" -print -quit 2>/dev/null)
    if [ -n "$matched" ]; then
      args+=("$matched")
    else
      # Pass through as-is if no match
      args+=("$a")
    fi
  fi
done

cd /home/pico || exit 1
exec /home/pico/apps/fuse/bin/fuse "${args[@]}"
