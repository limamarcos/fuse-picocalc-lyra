#!/usr/bin/env python3
"""
Build script for picocalc_snd_clean kernel module on Luckfox Lyra (Calculinux).
"""
import subprocess
import os
import sys

HEADERS_DIR = os.environ.get("KERNEL_HEADERS", "/home/pico/src/linux-headers")

if not os.path.isdir(HEADERS_DIR):
    print(f"Error: Kernel headers directory not found at {HEADERS_DIR}")
    sys.exit(1)

src_dir = os.path.dirname(os.path.abspath(__file__))
src_c = os.path.join(src_dir, "picocalc_snd_clean.c")

cflags = [
    "gcc",
    "-nostdinc",
    "-isystem", "/usr/lib/gcc/arm-poky-linux-musleabi/14.3.0/include",
    "-D__KERNEL__",
    "-DMODULE",
    "-march=armv7-a",
    "-mthumb",
    "-Wa,-mimplicit-it=always",
    "-Wa,-mno-warn-deprecated",
    "-D__LINUX_ARM_ARCH__=7",
    "-msoft-float",
    "-fno-pic", "-fno-pie", "-fno-PIC", "-fno-PIE",
    "-fno-strict-aliasing", "-fno-common", "-ffreestanding", "-fno-stack-protector",
    "-O2",
    "-Iinclude",
    "-Iarch/arm/include",
    "-Iarch/arm/include/generated",
    "-Iinclude/generated",
    "-Iinclude/uapi",
    "-Iarch/arm/include/uapi",
    "-Iarch/arm/include/generated/uapi",
    "-Iinclude/generated/uapi",
    "-include", "include/linux/compiler-version.h",
    "-include", "include/linux/kconfig.h"
]

os.chdir(HEADERS_DIR)

print("[*] Compiling picocalc_snd_clean.o...")
subprocess.run(cflags + [
    "-DKBUILD_BASENAME=\"picocalc_snd_clean\"",
    "-DKBUILD_MODNAME=\"picocalc_snd_clean\"",
    "-c", src_c,
    "-o", "/tmp/picocalc_snd_clean.o"
], check=True)

open("/tmp/picocalc_snd_clean.mod", "w").write("/tmp/picocalc_snd_clean.o\n")
open("/tmp/.picocalc_snd_clean.o.cmd", "w").write("")

print("[*] Generating module symbol table...")
subprocess.run([
    "scripts/mod/modpost", "-m", "-a", "-N", "-w",
    "-o", "/tmp/Module.symvers", "/tmp/picocalc_snd_clean.o"
], check=True)

mod_c = "/tmp/picocalc_snd_clean.mod.c"
txt = open(mod_c).read().replace("6.1.99-rockchip-standard+", "6.1.99-rockchip-standard")
open(mod_c, "w").write(txt)

print("[*] Compiling module stub...")
subprocess.run(cflags + [
    "-DKBUILD_MODNAME=\"picocalc_snd_clean\"",
    "-DKBUILD_BASENAME=\"picocalc_snd_clean.mod\"",
    "-c", mod_c,
    "-o", "/tmp/picocalc_snd_clean.mod.o"
], check=True)

print("[*] Linking final picocalc_snd_clean.ko...")
subprocess.run([
    "ld", "-r", "-T", "scripts/module.lds",
    "-o", "/tmp/picocalc_snd_clean.ko",
    "/tmp/picocalc_snd_clean.o",
    "/tmp/picocalc_snd_clean.mod.o"
], check=True)

print("[+] picocalc_snd_clean.ko compiled successfully at /tmp/picocalc_snd_clean.ko")
