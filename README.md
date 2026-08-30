# Fuse ZX Spectrum Emulator for PicoCalc (Luckfox Lyra / Calculinux)

[![Platform](https://img.shields.io/badge/Platform-PicoCalc%20%7C%20Luckfox%20Lyra-blue.svg)](https://github.com/limamarcos/fuse-picocalc-lyra)
[![OS](https://img.shields.io/badge/OS-Calculinux%20(Linux%206.1)-green.svg)](https://github.com/limamarcos/fuse-picocalc-lyra)
[![Sound](https://img.shields.io/badge/Audio-Zero--Whine%20ALSA%20Driver-brightgreen.svg)](https://github.com/limamarcos/fuse-picocalc-lyra)
[![Release](https://img.shields.io/badge/Release-v1.0-blueviolet.svg)](https://github.com/limamarcos/fuse-picocalc-lyra/releases)
[![License](https://img.shields.io/badge/License-GPL--2.0-yellow.svg)](LICENSE)

A complete, optimized port of **The Free Unix Spectrum Emulator (Fuse 1.9.1)** for the **PicoCalc** handheld calculator running **Calculinux** on the **Luckfox Lyra (Rockchip RV1103 / RK3506 Cortex-A55)**.

Includes the custom **Zero-Whine ALSA Audio Driver (`picocalc_snd_clean`)** that eliminates the piercing 8 kHz carrier noise and unlocks crystal-clear beeper and chiptune sound from the physical onboard speaker.

---

> [!NOTE]
> ### 🤖 AI Collaboration & "Vibe-Coding" Disclaimer
> This project was developed through an interactive **AI "vibe-coding" pairing** between **Marcos Lima**, **Kimi (Moonshot AI)**, and **Google DeepMind's Gemini (Antigravity)**:
> - **Kimi**: Led the initial porting of Fuse 1.9.1 and libspectrum to the Luckfox Lyra platform, framebuffer rendering integration, and game packaging.
> - **Gemini**: Diagnosed the physical hardware speaker routing (`GPIO4_B2`), identified the root cause of the 8 kHz carrier whine in the vendor software PWM driver, developed the custom zero-whine lockless softirq audio driver (`picocalc_snd_clean`), integrated native ALSA support into Fuse, and finalized the distribution package.
> 
> Released freely for all PicoCalc and retro-computing enthusiasts!

---

## ✨ Features

- **Crystal-Clear Speaker Audio**: Uses the custom `picocalc_snd_clean` kernel driver for direct GPIO4_B2 audio modulation with zero background whine and zero lockups.
- **Native ALSA Audio Engine**: Built directly against ALSA (`sound/alsasound.c`), eliminating piped child processes and latency.
- **Direct Framebuffer Graphics (`--with-fb`)**: Low-overhead hardware framebuffer rendering perfectly sized for the PicoCalc display.
- **PicoCalc Physical Keyboard Integration**: Pre-mapped controls for natural gaming on the calculator keypad.
- **Smart Game Launcher (`/usr/bin/fuse`)**: Resolves snapshot names automatically from `/home/pico/games/`.
- **Full Model Support**: Emulates Sinclair ZX Spectrum 16K, 48K, 128K, +2, +2A, +3, TC2048, and Pentagon.

---

## 🚀 Quick Install (One-Step)

Clone this repository or download the release archive onto your PicoCalc, then run the installer:

```bash
git clone https://github.com/limamarcos/fuse-picocalc-lyra.git
cd fuse-picocalc-lyra
sudo bash install.sh
```

The installer will automatically:
1. Install and load the `picocalc_snd_clean` kernel module.
2. Blacklist conflicting vendor drivers in `/etc/modprobe.d/sound.conf`.
3. Configure `/etc/asound.conf` for ALSA `plug` routing.
4. Install the Fuse binary, ROM files, and `/usr/bin/fuse` launcher.
5. Set up `~/.config/fuse-emulator/fuserc` with sound enabled.

---

## 🎮 How to Play

Launch any `.z80`, `.tap`, `.tzx`, or `.sna` game directly:

```bash
fuse "Manic Miner (1983)(Bug-Byte).z80"
```

Or specify a full path:
```bash
fuse /home/pico/games/willy2.z80
```

### Controls & Navigation
* **F1**: Fuse Widget Menu (File browser, Options, Machine selection, Snapshot save/load)
* **F2**: Save Snapshot
* **F3**: Open / Load File
* **F10**: Exit Emulator
* **Arrow Keys / Numeric Keys**: Standard Sinclair / Kempston / QAOP navigation

---

## 🛠 Technical Architecture: The Sound Fix

### The Problem
On the PicoCalc carrier board, the physical speaker is wired directly to **`GPIO4_B2`** (pin 138). The vendor software PWM driver (`picocalc_snd_softpwm`) operated at a fixed `125,000 ns` (8 kHz) period, producing a continuous, piercing 8 kHz carrier tone even when no sound was playing.

### The Solution (`picocalc_snd_clean.c`)
- **Dynamic Pulse Output**: The driver holds the output pin LOW (0V) during silence and between notes. High-frequency pulses are generated strictly when active audio samples are decoded.
- **SoftIRQ Architecture**: Uses `HRTIMER_MODE_REL_SOFT` and lockless atomic flags to ensure ALSA stream closures never trigger kernel spinlock deadlocks.

---

## 🔨 Building from Source

If you update your kernel or want to rebuild Fuse manually:

### 1. Build the Kernel Driver
```bash
cd fuse-picocalc-lyra/src
python3 build_module.py
sudo cp /tmp/picocalc_snd_clean.ko /lib/modules/$(uname -r)/extra/
sudo depmod -a
```

### 2. Build Fuse with Native ALSA
Ensure ALSA development headers are installed in `/usr/include/alsa/`:
```bash
cd /home/pico/src/fuse-1.9.1
PKG_CONFIG_PATH=/home/pico/apps/fuse/lib/pkgconfig ./configure \
    --prefix=/home/pico/apps/fuse \
    --with-fb \
    --disable-sdl2 \
    --without-gpm

make -j2
make install
```

---

## 📄 License & Credits

- **Fuse**: Distributed under the GNU General Public License v2 (GPL-2.0). Created by Philip Kendall and the Fuse Team.
- **picocalc_snd_clean**: Developed for the PicoCalc community under GPL-2.0.
- **Calculinux**: The PicoCalc Linux distribution.
