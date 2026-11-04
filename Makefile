# =============================================================================
#  Hyperion - A Modern C++26 UCI Chess Engine
#  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>
#
#  This file is part of Hyperion.
#
#  Hyperion is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  Hyperion is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <https://www.gnu.org/licenses/>.
# =============================================================================
#
#  Cross-platform Makefile.  Builds Hyperion for any combination of
#
#      OS:            linux | macos | windows
#      architecture:  x86_64 | arm64
#
#  from any of those hosts.  Works with GNU Make 3.81 or newer.
#
#  Quick start
#  -----------
#      make -j                         native build  ->  ./hyperion
#      make bench                      native build + built-in benchmark
#      make TARGET=windows-x86_64      cross build   ->  dist/hyperion-<ver>-windows-x86_64.exe
#      make release                    every target whose toolchain is installed -> dist/
#      make help                       full list of targets and options
#
# =============================================================================

NAME    := hyperion
VERSION := $(shell grep -m1 'define EngineVersion' src/uci.h 2>/dev/null | cut -d'"' -f2)

.DEFAULT_GOAL := all

# -----------------------------------------------------------------------------
# 1. Host detection
# -----------------------------------------------------------------------------

lc = $(shell echo '$(1)' | tr '[:upper:]' '[:lower:]')

ifeq ($(OS),Windows_NT)
    HOST_OS := windows
else
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Darwin)
        HOST_OS := macos
    else ifeq ($(UNAME_S),Linux)
        HOST_OS := linux
    else ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
        HOST_OS := windows
    else
        $(error Unsupported host OS '$(UNAME_S)')
    endif
endif

_HOST_M := $(call lc,$(shell uname -m 2>/dev/null || echo $(PROCESSOR_ARCHITECTURE)))
ifneq (,$(filter x86_64 amd64 x64,$(_HOST_M)))
    HOST_ARCH := x86_64
else ifneq (,$(filter arm64 aarch64 armv8%,$(_HOST_M)))
    HOST_ARCH := arm64
else
    $(error Unsupported host architecture '$(_HOST_M)')
endif

# -----------------------------------------------------------------------------
# 2. Target selection:  make TARGET=<os>-<arch>   (or TARGET_OS= / TARGET_ARCH=)
#      os   : linux | macos | windows
#      arch : x86_64 | arm64
# -----------------------------------------------------------------------------

ifdef TARGET
    TARGET_OS   ?= $(word 1,$(subst -, ,$(TARGET)))
    TARGET_ARCH ?= $(word 2,$(subst -, ,$(TARGET)))
endif
TARGET_OS   ?= $(HOST_OS)
TARGET_ARCH ?= $(HOST_ARCH)

_T_OS := $(call lc,$(TARGET_OS))
ifneq (,$(filter darwin mac macos osx,$(_T_OS)))
    override TARGET_OS := macos
else ifneq (,$(filter win windows win32 win64 mingw mingw32 mingw64,$(_T_OS)))
    override TARGET_OS := windows
else ifneq (,$(filter linux,$(_T_OS)))
    override TARGET_OS := linux
else
    $(error Unknown TARGET_OS '$(TARGET_OS)' (use linux, macos or windows))
endif

_T_ARCH := $(call lc,$(TARGET_ARCH))
ifneq (,$(filter x86_64 amd64 x64,$(_T_ARCH)))
    override TARGET_ARCH := x86_64
else ifneq (,$(filter arm64 aarch64 armv8,$(_T_ARCH)))
    override TARGET_ARCH := arm64
else
    $(error Unknown TARGET_ARCH '$(TARGET_ARCH)' (use x86_64 or arm64))
endif

TARGET_TRIPLET := $(TARGET_OS)-$(TARGET_ARCH)
ALL_TARGETS    := linux-x86_64 linux-arm64 macos-x86_64 macos-arm64 windows-x86_64 windows-arm64

CROSS := $(if $(and $(filter $(HOST_OS),$(TARGET_OS)),$(filter $(HOST_ARCH),$(TARGET_ARCH))),0,1)
EXE   := $(if $(filter windows,$(TARGET_OS)),.exe,)

# PORTABLE=1 builds for a generic CPU of the target architecture (required for
# anything you redistribute).  The default for native builds is -march=native.
PORTABLE ?= $(CROSS)

# Where the binary goes: ./hyperion for local builds, dist/ for cross/portable.
OUT ?= $(if $(filter 1,$(CROSS) $(PORTABLE)),dist/$(NAME)-$(VERSION)-$(TARGET_TRIPLET)$(EXE),$(NAME)$(EXE))

BUILD_DIR := build/$(TARGET_TRIPLET)-$(if $(filter 1,$(PORTABLE)),portable,native)$(if $(DEBUG),-debug,)

# -----------------------------------------------------------------------------
# 3. Toolchain selection
#
#    TOOLCHAIN=auto   (default) native compiler, else a prefixed GNU/LLVM cross
#                     compiler found in PATH, else zig.
#    TOOLCHAIN=zig    `zig c++` - one toolchain for every OS/arch pair
#                     (https://ziglang.org, no sysroots needed).
#    CXX=<compiler>   use exactly this compiler (e.g. CXX=clang++ -target ...).
# -----------------------------------------------------------------------------

TOOLCHAIN ?= auto

have = $(firstword $(foreach c,$(1),$(if $(shell command -v $(c) 2>/dev/null),$(c))))

# Cross-compiler candidates, keyed by <os>_<arch>.
CAND_linux_x86_64   := x86_64-linux-gnu-g++ x86_64-unknown-linux-gnu-g++ x86_64-linux-musl-g++
CAND_linux_arm64    := aarch64-linux-gnu-g++ aarch64-unknown-linux-gnu-g++ aarch64-linux-musl-g++
CAND_windows_x86_64 := x86_64-w64-mingw32-g++ x86_64-w64-mingw32-clang++
CAND_windows_arm64  := aarch64-w64-mingw32-g++ aarch64-w64-mingw32-clang++
CAND_macos_x86_64   := o64-clang++ x86_64-apple-darwin-clang++
CAND_macos_arm64    := oa64-clang++ aarch64-apple-darwin-clang++ arm64-apple-darwin-clang++

# Target triples understood by `zig c++ -target`.
ZIG_linux_x86_64   := x86_64-linux-gnu
ZIG_linux_arm64    := aarch64-linux-gnu
ZIG_windows_x86_64 := x86_64-windows-gnu
ZIG_windows_arm64  := aarch64-windows-gnu
ZIG_macos_x86_64   := x86_64-macos
ZIG_macos_arm64    := aarch64-macos

_KEY := $(TARGET_OS)_$(TARGET_ARCH)
TOOLCHAIN_ERROR :=
ARCH_DRIVER_FLAGS :=

ifneq ($(origin CXX),default)
    TOOLCHAIN_NAME := custom ($(CXX))
else ifeq ($(TOOLCHAIN),zig)
    ifeq ($(call have,zig),)
        TOOLCHAIN_ERROR := TOOLCHAIN=zig requested but 'zig' is not in PATH (https://ziglang.org/download)
    endif
    CXX := zig c++ -target $(ZIG_$(_KEY))
    TOOLCHAIN_NAME := zig
else ifeq ($(CROSS),0)
    TOOLCHAIN_NAME := native
    ifeq ($(HOST_OS),macos)
        CXX := clang++
    else ifeq ($(HOST_OS),linux)
        CXX := $(or $(call have,g++),$(call have,clang++),g++)
    else
        CXX := g++
    endif
else ifeq ($(and $(filter macos,$(HOST_OS)),$(filter macos,$(TARGET_OS))),macos)
    # macOS -> other macOS architecture: Apple clang is a multi-arch compiler.
    CXX := clang++
    ARCH_DRIVER_FLAGS := -arch $(if $(filter arm64,$(TARGET_ARCH)),arm64,x86_64)
    TOOLCHAIN_NAME := apple clang (-arch)
else
    _FOUND := $(call have,$(CAND_$(_KEY)))
    ifneq ($(_FOUND),)
        CXX := $(_FOUND)
        TOOLCHAIN_NAME := $(_FOUND)
    else ifneq ($(call have,zig),)
        CXX := zig c++ -target $(ZIG_$(_KEY))
        TOOLCHAIN_NAME := zig (fallback)
    else
        CXX := false
        TOOLCHAIN_NAME := none
        TOOLCHAIN_ERROR := no toolchain found for $(TARGET_TRIPLET) on $(HOST_OS)-$(HOST_ARCH). Install one of: $(CAND_$(_KEY)) - or install zig and use TOOLCHAIN=zig
    endif
endif

# Highest C++ standard flag the selected compiler understands.
ifeq ($(TOOLCHAIN_ERROR),)
    STD ?= $(shell echo | $(CXX) -std=c++26 -x c++ -fsyntax-only - >/dev/null 2>&1 && echo c++26 || echo c++2c)
else
    STD ?= c++26
endif

# -----------------------------------------------------------------------------
# 4. Compiler and linker flags
# -----------------------------------------------------------------------------

INCLUDES := -Isrc -Isrc/stockfish_probe

ifdef DEBUG
    OPTFLAGS := -O0 -g
else
    OPTFLAGS := -O3 -DNDEBUG $(if $(filter 0,$(LTO)),,-flto)
endif

WARNFLAGS := -Wall -Wextra -Wshadow
CXXFLAGS  += -std=$(STD) $(OPTFLAGS) $(WARNFLAGS) -fno-exceptions -pthread $(INCLUDES) $(ARCH_DRIVER_FLAGS)
LDFLAGS   += $(OPTFLAGS) -pthread $(ARCH_DRIVER_FLAGS)

ifdef SANITIZE
    CXXFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
    LDFLAGS  += -fsanitize=$(SANITIZE)
endif

# ---- CPU architecture ------------------------------------------------------
ifeq ($(TARGET_ARCH),x86_64)
    # NNUE needs AVX2 (Haswell / Zen 2 or newer).  x86-64-v3 is the portable baseline.
    X86_LEVEL ?= x86-64-v3
    ifeq ($(PORTABLE),1)
        CXXFLAGS += -march=$(X86_LEVEL)
    else
        CXXFLAGS += -march=native
    endif
    CXXFLAGS += -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2 -mbmi2
else
    # ARMv8 with dot-product instructions (all Apple Silicon, Graviton 2+, Snapdragon X, ...).
    CXXFLAGS += -DUSE_NEON=8
    ifeq ($(PORTABLE),1)
        ifeq ($(TARGET_OS),macos)
            CXXFLAGS += -mcpu=apple-m1
        else
            CXXFLAGS += -march=armv8.2-a+dotprod
        endif
    else
        CXXFLAGS += -mcpu=native
    endif
endif

# ---- Operating system ------------------------------------------------------
ifeq ($(TARGET_OS),windows)
    # Static runtime: no MinGW DLLs next to the .exe.  The PE stack reserve is the
    # default stack size of every thread, so deep searches need it raised.
    LDFLAGS += -static -Wl,--stack,16777216
else ifeq ($(TARGET_OS),linux)
    # Cross-built Linux binaries are linked statically so they run on any distro.
    STATIC ?= $(CROSS)
    ifeq ($(STATIC),1)
        LDFLAGS += -static
    endif
endif

ifeq ($(STRIP),1)
    ifneq ($(TARGET_OS),macos)
        LDFLAGS += -s
    endif
endif

# -----------------------------------------------------------------------------
# 5. Sources and objects
# -----------------------------------------------------------------------------

SRCS := main.cpp bitboard.cpp tt.cpp position.cpp movgen.cpp eval.cpp search.cpp \
        uci.cpp tbprobe.cpp thread.cpp crash_recovery.cpp \
        stockfish_probe/probe.cpp stockfish_probe/position.cpp stockfish_probe/bitboard.cpp \
        stockfish_probe/evaluate.cpp stockfish_probe/misc.cpp stockfish_probe/memory.cpp \
        stockfish_probe/nnue_incremental.cpp stockfish_probe/nnue/evaluate_nnue.cpp \
        stockfish_probe/nnue/features/half_ka_v2_hm.cpp

OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.cpp=.o))
DEPS := $(OBJS:.o=.d)

NETS := nn-b1a57edbea57.nnue nn-baff1ede1f90.nnue

NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# V=1 prints the full compiler command lines.
Q := $(if $(V),,@)

# -----------------------------------------------------------------------------
# 6. Targets
# -----------------------------------------------------------------------------

.PHONY: all build bench release targets probe info check-toolchain clean distclean install help

# `make` builds in parallel by default; `make -jN` still takes precedence.
all:
	@$(MAKE) --no-print-directory -j$(NPROC) build

build: info check-toolchain $(OUT)

info:
	@echo "Hyperion $(VERSION): $(HOST_OS)-$(HOST_ARCH) -> $(TARGET_TRIPLET) | toolchain: $(TOOLCHAIN_NAME) | -std=$(STD) | $(if $(filter 1,$(PORTABLE)),portable,native CPU) -> $(OUT)"

check-toolchain:
	@if [ -n "$(TOOLCHAIN_ERROR)" ]; then echo "error: $(TOOLCHAIN_ERROR)" >&2; exit 1; fi

# Exits successfully when a toolchain exists for the selected TARGET (used by `release`).
probe:
	@test -z "$(TOOLCHAIN_ERROR)"

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "  LINK  $@"
	$(Q)$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $<"
	$(Q)$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Native build, then run the built-in benchmark (node count is a regression fingerprint).
bench: all
	./$(OUT) bench $(or $(DEPTH),12)

# Build every target whose toolchain is available into dist/ (portable CPU level, stripped).
release:
	@mkdir -p dist
	@for t in $(ALL_TARGETS); do \
	    if $(MAKE) --no-print-directory -s TARGET=$$t PORTABLE=1 probe >/dev/null 2>&1; then \
	        echo "==> $$t"; \
	        $(MAKE) --no-print-directory -j$(NPROC) TARGET=$$t PORTABLE=1 STRIP=1 build || echo "!! $$t FAILED"; \
	    else \
	        echo "==> $$t: skipped (no toolchain; install one or use TOOLCHAIN=zig)"; \
	    fi; \
	done
	@cp -f $(NETS) dist/ 2>/dev/null || true
	@echo "Release binaries:" && ls -lh dist | grep $(NAME) || true

targets:
	@echo "Supported targets (make TARGET=<os>-<arch>):"
	@for t in $(ALL_TARGETS); do echo "  $$t"; done
	@echo "Host: $(HOST_OS)-$(HOST_ARCH)"

# PREFIX=/usr/local make install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/hyperion
	install -m 755 $(OUT) $(DESTDIR)$(PREFIX)/bin/$(NAME)$(EXE)
	install -m 644 $(NETS) $(DESTDIR)$(PREFIX)/share/hyperion/
PREFIX ?= /usr/local

clean:
	rm -rf build $(NAME) $(NAME).exe

distclean: clean
	rm -rf dist

help:
	@echo "Hyperion $(VERSION) - build system"
	@echo ""
	@echo "Usage: make [target] [option=value ...]"
	@echo ""
	@echo "Targets:"
	@echo "  all (default)    Build for the target (parallel). Native: ./$(NAME); cross: dist/"
	@echo "  bench            Native build + 'hyperion bench' (DEPTH=12 by default)"
	@echo "  release          Build every target with an available toolchain into dist/"
	@echo "  targets          List supported <os>-<arch> targets"
	@echo "  install          Install to PREFIX (default /usr/local)"
	@echo "  clean/distclean  Remove build/ and binaries (distclean also removes dist/)"
	@echo ""
	@echo "Options:"
	@echo "  TARGET=<os>-<arch>   linux|macos|windows - x86_64|arm64 (default: host $(HOST_OS)-$(HOST_ARCH))"
	@echo "  TOOLCHAIN=auto|zig   Cross toolchain: PATH-detected GNU/LLVM compilers, or 'zig c++'"
	@echo "  CXX=<compiler>       Use exactly this compiler"
	@echo "  PORTABLE=0|1         1 = generic CPU baseline (x86-64-v3 / armv8.2+dotprod); default 1 when cross"
	@echo "  X86_LEVEL=x86-64-v3  x86_64 baseline for portable builds (v3 = AVX2, v4 = AVX-512)"
	@echo "  STATIC=0|1           Static linking on Linux (default 1 when cross compiling)"
	@echo "  STRIP=1              Strip symbols    LTO=0  Disable link-time optimisation"
	@echo "  DEBUG=1              -O0 -g           SANITIZE=address|undefined|thread"
	@echo "  V=1                  Echo compiler command lines"
	@echo ""
	@echo "Cross toolchains (any one per target):"
	@echo "  linux-x86_64     x86_64-linux-gnu-g++      linux-arm64     aarch64-linux-gnu-g++"
	@echo "  windows-x86_64   x86_64-w64-mingw32-g++    windows-arm64   aarch64-w64-mingw32-clang++ (llvm-mingw)"
	@echo "  macos-*          clang++ on macOS, osxcross (o64-clang++/oa64-clang++) elsewhere"
	@echo "  any target       zig (TOOLCHAIN=zig) - single toolchain, no sysroots"
	@echo ""
	@echo "Examples:"
	@echo "  make -j                              # native"
	@echo "  make TARGET=windows-x86_64           # Windows .exe via MinGW-w64"
	@echo "  make TARGET=linux-arm64 TOOLCHAIN=zig"
	@echo "  make release                         # everything available"
