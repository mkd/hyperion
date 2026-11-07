#!/usr/bin/env python3
"""
Hyperion - Cross-Platform Multi-Target Build Tool
Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later. See LICENSE; no warranty.

Detects host platform, probes target toolchains (GCC, Clang, MinGW, Apple Clang),
and drives the Makefile to produce ultra-optimized native or cross-compiled binaries
with full SIMD vector acceleration and C++26 optimizations.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

MANDATORY_TARGETS = [
    "macos-arm64",
    "linux-x86_64",
    "linux-arm64",
    "windows-x86_64",
    "windows-arm64",
]

ARCH_OPTIONS = {
    "x86_64": ["x86-64-avx512", "x86-64-avx2", "x86-64-bmi2", "x86-64-modern"],
    "arm64": ["apple-silicon", "arm64-dotprod", "arm64-neon"],
}

PREFIXES = {
    "linux-x86_64": ["x86_64-linux-gnu-g++", "x86_64-linux-gnu-clang++", "x86_64-unknown-linux-gnu-g++"],
    "linux-arm64": ["aarch64-linux-gnu-g++", "aarch64-linux-gnu-clang++", "aarch64-unknown-linux-gnu-g++"],
    "windows-x86_64": ["x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-clang++"],
    "windows-arm64": ["aarch64-w64-mingw32-g++", "aarch64-w64-mingw32-clang++"],
}


def detect_host() -> tuple[str, str]:
    system = platform.system().lower()
    if system == "darwin":
        os_name = "macos"
    elif system == "linux":
        os_name = "linux"
    elif system in ("windows", "cygwin", "msys"):
        os_name = "windows"
    else:
        os_name = system

    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64", "x64"):
        arch = "x86_64"
    elif machine in ("arm64", "aarch64"):
        arch = "arm64"
    else:
        arch = machine

    return os_name, arch


def find_compiler(target: str, host_os: str, host_arch: str) -> str | None:
    host_target = f"{host_os}-{host_arch}"
    if target == host_target:
        for c in ["clang++", "g++", "c++"]:
            p = shutil.which(c)
            if p:
                return p
    for candidate in PREFIXES.get(target, []):
        p = shutil.which(candidate)
        if p:
            return p
    return None


def probe_toolchains() -> dict[str, dict]:
    host_os, host_arch = detect_host()
    host_target = f"{host_os}-{host_arch}"
    status = {}
    for t in MANDATORY_TARGETS:
        compiler = find_compiler(t, host_os, host_arch)
        is_native = (t == host_target)
        status[t] = {
            "target": t,
            "native": is_native,
            "compiler": compiler,
            "available": compiler is not None,
        }
    return status


def run_make(target: str, arch: str | None = None, jobs: int | None = None,
             extra_args: list[str] | None = None) -> int:
    cmd = ["make"]
    if jobs:
        cmd.append(f"-j{jobs}")
    else:
        cmd.append("-j")

    cmd.append(f"TARGET={target}")
    if arch:
        cmd.append(f"ARCH={arch}")
    
    # Ensure network is ready
    cmd.append("PORTABLE=1")

    if extra_args:
        cmd.extend(extra_args)

    cmd.append("build")
    print(f"==> Running: {' '.join(cmd)} in {ROOT}")
    res = subprocess.run(cmd, cwd=ROOT)
    return res.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Hyperion Cross-Platform Multi-Target Build Tool"
    )
    parser.add_argument(
        "--target",
        choices=MANDATORY_TARGETS,
        help="Target platform (default: native host target)",
    )
    parser.add_argument(
        "--arch",
        help="Sub-architecture SIMD tuning (e.g. apple-silicon, x86-64-avx512, x86-64-avx2, arm64-dotprod)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Build all mandatory targets that have an available toolchain into dist/",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Probe and list available target compilers without building",
    )
    parser.add_argument(
        "-j", "--jobs",
        type=int,
        default=None,
        help="Number of concurrent compile jobs",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build directories first",
    )
    parser.add_argument(
        "--bench",
        action="store_true",
        help="Run bench after building (native only)",
    )

    args = parser.parse_args()
    host_os, host_arch = detect_host()
    host_target = f"{host_os}-{host_arch}"

    if args.check:
        print(f"Host: {host_target} ({platform.platform()})\n")
        print("Available Toolchain Matrix:")
        print("------------------------------------------------------------")
        matrix = probe_toolchains()
        for t, info in matrix.items():
            avail = "READY" if info["available"] else "MISSING"
            nat = " [NATIVE]" if info["native"] else ""
            comp = info["compiler"] or "none found in PATH"
            print(f"  {t:<16} : {avail:<8}{nat:<10} (compiler: {comp})")
        print("------------------------------------------------------------")
        return 0

    if args.clean:
        print("==> Cleaning build artifacts...")
        subprocess.run(["make", "clean"], cwd=ROOT)

    if args.all:
        print(f"==> Building all available mandatory targets for release...")
        cmd = ["make", "release"]
        if args.jobs:
            cmd.append(f"NPROC={args.jobs}")
        return subprocess.run(cmd, cwd=ROOT).returncode

    target = args.target or host_target
    arch = args.arch
    if not arch:
        if target == "macos-arm64":
            arch = "apple-silicon"
        elif "arm64" in target:
            arch = "arm64-dotprod"
        else:
            arch = "x86-64-avx2"

    ret = run_make(target, arch, jobs=args.jobs)
    if ret != 0:
        print(f"ERROR: Build failed for {target} [{arch}]", file=sys.stderr)
        return ret

    if args.bench:
        if target == host_target:
            print("==> Running native bench...")
            subprocess.run(["make", "bench"], cwd=ROOT)
        else:
            print(f"NOTE: Target {target} is a cross-compiled binary, cannot run bench on host.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
