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
#  Ultra-portable cross-platform Makefile. Builds Hyperion for:
#
#      OS:            linux | macos | windows
#      architecture:  x86_64 (AVX-512, AVX2, BMI2, SSE4.1) | arm64 (NEON, dotprod, Apple Silicon)
#
#  from any host with peak hardware acceleration and full C++26 optimizations.
#
#  Quick start
#  -----------
#      make -j                         native build  ->  ./hyperion
#      make bench                      native build + built-in benchmark
#      make apple-silicon              optimized for Apple Silicon M-series
#      make x86-64-avx512              AVX-512 optimized build
#      make x86-64-avx2                AVX2 / BMI2 optimized build
#      make windows-x86_64             cross-compile for Windows x86_64 (.exe)
#      make windows-arm64              cross-compile for Windows ARM64 (.exe)
#      make linux-x86_64               cross-compile for Linux x86_64
#      make linux-arm64                cross-compile for Linux ARM64
#      make release                    build all supported platforms into dist/
#      make net                        fetch/validate NNUE network
#      make help                       full documentation of targets and flags
#
# =============================================================================

NAME    := hyperion
VERSION := $(shell grep -m1 'define EngineVersion' src/uci.h 2>/dev/null | cut -d'"' -f2)

.DEFAULT_GOAL := all

SHELL := /bin/sh

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
# 2. Target selection:  make TARGET=<os>-<arch> [ARCH=<sub-arch>]
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
ALL_TARGETS    := linux-x86_64 linux-arm64 macos-arm64 windows-x86_64 windows-arm64

CROSS := $(if $(and $(filter $(HOST_OS),$(TARGET_OS)),$(filter $(HOST_ARCH),$(TARGET_ARCH))),0,1)
EXE   := $(if $(filter windows,$(TARGET_OS)),.exe,)

PORTABLE ?= $(CROSS)

# Sub-architecture defaults
ifeq ($(ARCH),)
    ifeq ($(TARGET_ARCH),arm64)
        ifeq ($(TARGET_OS),macos)
            ARCH := apple-silicon
        else
            ARCH := arm64-dotprod
        endif
    else
        ifeq ($(CROSS),1)
            ARCH := x86-64-avx2
        else
            ARCH := native
        endif
    endif
endif

# Where the binary goes
OUT ?= $(if $(filter 1,$(CROSS) $(PORTABLE)),dist/$(NAME)-$(VERSION)-$(TARGET_TRIPLET)-$(ARCH)$(EXE),$(NAME)$(EXE))

BUILD_DIR := build/$(TARGET_TRIPLET)-$(ARCH)$(if $(filter 1,$(PORTABLE)),-portable,)$(if $(DEBUG),-debug,)

# -----------------------------------------------------------------------------
# 3. Toolchain selection
# -----------------------------------------------------------------------------

TOOLCHAIN ?= auto
have = $(firstword $(foreach c,$(1),$(if $(shell command -v $(c) 2>/dev/null),$(c))))

CAND_linux_x86_64   := x86_64-linux-gnu-g++ x86_64-unknown-linux-gnu-g++ x86_64-linux-musl-g++
CAND_linux_arm64    := aarch64-linux-gnu-g++ aarch64-unknown-linux-gnu-g++ aarch64-linux-musl-g++
CAND_windows_x86_64 := x86_64-w64-mingw32-g++ x86_64-w64-mingw32-clang++
CAND_windows_arm64  := aarch64-w64-mingw32-clang++ aarch64-w64-mingw32-g++
CAND_macos_arm64    := oa64-clang++ aarch64-apple-darwin-clang++ arm64-apple-darwin-clang++

ZIG_linux_x86_64   := x86_64-linux-gnu
ZIG_linux_arm64    := aarch64-linux-gnu
ZIG_windows_x86_64 := x86_64-windows-gnu
ZIG_windows_arm64  := aarch64-windows-gnu
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
    CXX := clang++
    ARCH_DRIVER_FLAGS := -arch arm64
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
        TOOLCHAIN_ERROR := no toolchain found for $(TARGET_TRIPLET) on $(HOST_OS)-$(HOST_ARCH). Install $(CAND_$(_KEY)) or install zig (https://ziglang.org)
    endif
endif

# Highest C++ standard flag
ifeq ($(TOOLCHAIN_ERROR),)
    STD ?= $(shell echo | $(CXX) -std=c++26 -x c++ -fsyntax-only - >/dev/null 2>&1 && echo c++26 || echo c++2c)
else
    STD ?= c++26
endif

# -----------------------------------------------------------------------------
# 4. Compiler and linker flags (Acceleration & SIMD)
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

# Hardware SIMD and instruction set features
ifeq ($(TARGET_ARCH),x86_64)
    ifeq ($(ARCH),x86-64-avx512)
        CXXFLAGS += -mavx512f -mavx512bw -mavx512dq -mavx512vl -mbmi -mbmi2 -mpopcnt \
                    -DUSE_AVX512 -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2
    else ifeq ($(ARCH),x86-64-avx2)
        CXXFLAGS += -mavx2 -mbmi -mbmi2 -mpopcnt -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2
    else ifeq ($(ARCH),x86-64-bmi2)
        CXXFLAGS += -mavx2 -mbmi2 -mpopcnt -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2
    else ifeq ($(ARCH),x86-64-modern)
        CXXFLAGS += -msse4.1 -mpopcnt -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2
    else ifeq ($(ARCH),x86-64-ssse3)
        CXXFLAGS += -mssse3 -DUSE_SSSE3 -DUSE_SSE2
    else
        # native build on x86_64
        ifeq ($(PORTABLE),1)
            CXXFLAGS += -march=x86-64-v3 -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2 -mbmi2
        else
            CXXFLAGS += -march=native -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2 -mbmi2
        endif
    endif
else
    # ARM64
    CXXFLAGS += -DUSE_NEON=8 -DUSE_NEON_DOTPROD
    ifeq ($(ARCH),apple-silicon)
        ifeq ($(PORTABLE),1)
            CXXFLAGS += -mcpu=apple-m1
        else
            CXXFLAGS += $(if $(filter macos,$(HOST_OS)),-mcpu=native,-mcpu=apple-m1)
        endif
    else ifeq ($(ARCH),arm64-neon)
        override CXXFLAGS := $(filter-out -DUSE_NEON_DOTPROD,$(CXXFLAGS))
        CXXFLAGS += -march=armv8-a
    else
        ifeq ($(PORTABLE),1)
            CXXFLAGS += -march=armv8.2-a+dotprod
        else
            CXXFLAGS += -mcpu=native
        endif
    endif
endif

# Operating system specific flags
ifeq ($(TARGET_OS),windows)
    LDFLAGS += -static -Wl,--stack,16777216
else ifeq ($(TARGET_OS),linux)
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
# 5. Sources and objects (SFNNv16 Architecture)
# -----------------------------------------------------------------------------

SRCS := main.cpp bitboard.cpp tt.cpp position.cpp movgen.cpp eval.cpp search.cpp \
        uci.cpp tbprobe.cpp thread.cpp crash_recovery.cpp \
        stockfish_probe/probe.cpp stockfish_probe/position.cpp stockfish_probe/bitboard.cpp \
        stockfish_probe/evaluate.cpp stockfish_probe/misc.cpp stockfish_probe/memory.cpp \
        stockfish_probe/attacks.cpp \
        stockfish_probe/nnue_incremental.cpp stockfish_probe/nnue/nnue_misc.cpp \
        stockfish_probe/nnue/nnue_accumulator.cpp stockfish_probe/nnue/network.cpp \
        stockfish_probe/nnue/features/full_threats.cpp stockfish_probe/nnue/features/half_ka_v2_hm.cpp \
        stockfish_probe/nnue/features/pp_3wide.cpp

OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.cpp=.o))
DEPS := $(OBJS:.o=.d)

NET := nn-1a298aa575a0.nnue

NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

Q := $(if $(V),,@)

# -----------------------------------------------------------------------------
# 6. Targets
# -----------------------------------------------------------------------------

.PHONY: all build bench release targets probe info check-toolchain clean distclean install help net config-sanity \
        x86-64-avx512 x86-64-avx2 x86-64-bmi2 x86-64-modern apple-silicon arm64 \
        linux-x86_64 linux-arm64 windows-x86_64 windows-arm64 macos-arm64

all:
	@$(MAKE) --no-print-directory -j$(NPROC) build

build: net info check-toolchain $(OUT)

net:
	@$(SHELL) scripts/net.sh

config-sanity: net

info:
	@echo "Hyperion $(VERSION): $(HOST_OS)-$(HOST_ARCH) -> $(TARGET_TRIPLET) [$(ARCH)] | toolchain: $(TOOLCHAIN_NAME) | -std=$(STD) | $(if $(filter 1,$(PORTABLE)),portable,native CPU) -> $(OUT)"

check-toolchain:
	@if [ -n "$(TOOLCHAIN_ERROR)" ]; then echo "error: $(TOOLCHAIN_ERROR)" >&2; exit 1; fi

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

# Benchmark
bench: all
	./$(OUT) bench $(or $(DEPTH),12)

# Convenience target shortcuts
apple-silicon:
	@$(MAKE) --no-print-directory TARGET=macos-arm64 ARCH=apple-silicon build

x86-64-avx512:
	@$(MAKE) --no-print-directory TARGET=$(HOST_OS)-x86_64 ARCH=x86-64-avx512 build

x86-64-avx2:
	@$(MAKE) --no-print-directory TARGET=$(HOST_OS)-x86_64 ARCH=x86-64-avx2 build

x86-64-bmi2:
	@$(MAKE) --no-print-directory TARGET=$(HOST_OS)-x86_64 ARCH=x86-64-bmi2 build

x86-64-modern:
	@$(MAKE) --no-print-directory TARGET=$(HOST_OS)-x86_64 ARCH=x86-64-modern build

arm64:
	@$(MAKE) --no-print-directory TARGET=$(HOST_OS)-arm64 ARCH=arm64-dotprod build

linux-x86_64:
	@$(MAKE) --no-print-directory TARGET=linux-x86_64 ARCH=x86-64-avx2 PORTABLE=1 build

linux-arm64:
	@$(MAKE) --no-print-directory TARGET=linux-arm64 ARCH=arm64-dotprod PORTABLE=1 build

windows-x86_64:
	@$(MAKE) --no-print-directory TARGET=windows-x86_64 ARCH=x86-64-avx2 PORTABLE=1 build

windows-arm64:
	@$(MAKE) --no-print-directory TARGET=windows-arm64 ARCH=arm64-dotprod PORTABLE=1 build

macos-arm64:
	@$(MAKE) --no-print-directory TARGET=macos-arm64 ARCH=apple-silicon PORTABLE=1 build

# Build every target whose toolchain is available into dist/
release: net
	@mkdir -p dist
	@for t in $(ALL_TARGETS); do \
	    if $(MAKE) --no-print-directory -s TARGET=$$t PORTABLE=1 probe >/dev/null 2>&1; then \
	        echo "==> $$t"; \
	        $(MAKE) --no-print-directory -j$(NPROC) TARGET=$$t PORTABLE=1 STRIP=1 build || echo "!! $$t FAILED"; \
	    else \
	        echo "==> $$t: skipped (no toolchain; install one or use TOOLCHAIN=zig)"; \
	    fi; \
	done
	@cp -f $(NET) dist/ 2>/dev/null || true
	@echo "Release binaries:" && ls -lh dist | grep $(NAME) || true

targets:
	@echo "Supported targets (make TARGET=<os>-<arch>):"
	@for t in $(ALL_TARGETS); do echo "  $$t"; done
	@echo "Supported architectures: apple-silicon, x86-64-avx512, x86-64-avx2, x86-64-bmi2, x86-64-modern, arm64-dotprod, arm64-neon"
	@echo "Host: $(HOST_OS)-$(HOST_ARCH)"

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/hyperion
	install -m 755 $(OUT) $(DESTDIR)$(PREFIX)/bin/$(NAME)$(EXE)
	install -m 644 $(NET) $(DESTDIR)$(PREFIX)/share/hyperion/
PREFIX ?= /usr/local

clean:
	rm -rf build $(NAME) $(NAME).exe

distclean: clean
	rm -rf dist

help:
	@echo "Hyperion $(VERSION) - Ultra-Portable High Performance Build System"
	@echo ""
	@echo "Usage: make [target] [option=value ...]"
	@echo ""
	@echo "Targets:"
	@echo "  all (default)    Build for the target (parallel). Native: ./$(NAME); cross: dist/"
	@echo "  bench            Native build + 'hyperion bench' (DEPTH=12 by default)"
	@echo "  net              Download and validate NNUE file ($(NET)) if missing"
	@echo "  release          Build every target with an available toolchain into dist/"
	@echo "  apple-silicon    Build for Apple Silicon M-series (NEON + dotprod)"
	@echo "  x86-64-avx512    Build for x86_64 AVX-512"
	@echo "  x86-64-avx2      Build for x86_64 AVX2 / BMI2"
	@echo "  x86-64-modern    Build for x86_64 SSE4.1 / popcnt"
	@echo "  linux-x86_64     Build Linux x86_64 binary"
	@echo "  linux-arm64      Build Linux ARM64 binary"
	@echo "  windows-x86_64   Build Windows x86_64 (.exe) binary"
	@echo "  windows-arm64    Build Windows ARM64 (.exe) binary"
	@echo "  macos-arm64      Build macOS ARM64 binary"
	@echo "  clean/distclean  Remove build/ and binaries"
	@echo ""
	@echo "Options:"
	@echo "  TARGET=<os>-<arch>   linux|macos|windows - x86_64|arm64"
	@echo "  ARCH=<arch>          apple-silicon|x86-64-avx512|x86-64-avx2|x86-64-modern|arm64-dotprod"
	@echo "  TOOLCHAIN=auto|zig   Cross toolchain: PATH-detected GNU/LLVM compilers, or 'zig c++'"
	@echo "  CXX=<compiler>       Explicit C++ compiler executable"
	@echo "  PORTABLE=0|1         1 = generic CPU baseline; default 1 when cross-compiling"
	@echo "  STRIP=1              Strip symbols"
	@echo "  LTO=0|1              Link-time optimization (default 1 in release)"
	@echo "  DEBUG=1              Debug build (-O0 -g)"
	@echo "  SANITIZE=...         Address/undefined/thread sanitizers"
