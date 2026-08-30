#!/bin/bash
# PicoCalc launcher for the Fuse ZX Spectrum emulator.
# - runs from any directory
# - bare snapshot names (e.g. fuse "serpa.z80") are looked up in
#   /home/pico/games
# - always uses pico's config (~/.config/fuse-emulator/fuserc, sound off)
#   regardless of which user invokes it
export HOME=/home/pico
GAMES=/home/pico/games

args=()
for a in "$@"; do
  if [[ $a != -* && ! -e $a && -e $GAMES/$a ]]; then
    a=$GAMES/$a
  fi
  args+=("$a")
done

cd /home/pico || exit 1
exec /home/pico/apps/fuse/bin/fuse "${args[@]}"
