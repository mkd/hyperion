# Makefile for Hyperion (C++26 UCI Chess Engine)
CXX = clang++
CXXFLAGS = -Wall -Wextra -Wshadow -std=c++26 -O3 -flto -fno-exceptions -DNDEBUG -DUSE_NEW_NNUE -I. -Istockfish_probe

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),arm64)
    CXXFLAGS += -DUSE_NEON=8 -mcpu=native
else ifeq ($(UNAME_M),aarch64)
    CXXFLAGS += -DUSE_NEON=8 -mcpu=native
else
    CXXFLAGS += -march=native -DUSE_AVX2 -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2 -mbmi2
endif

LDFLAGS = -lpthread -flto

SRCS = main.cpp bitboard.cpp tt.cpp position.cpp movgen.cpp eval.cpp search.cpp uci.cpp tbprobe.cpp thread.cpp crash_recovery.cpp \
       stockfish_probe/probe.cpp stockfish_probe/position.cpp stockfish_probe/bitboard.cpp stockfish_probe/evaluate.cpp \
       stockfish_probe/misc.cpp stockfish_probe/memory.cpp stockfish_probe/nnue_incremental.cpp \
       stockfish_probe/nnue/evaluate_nnue.cpp stockfish_probe/nnue/features/half_ka_v2_hm.cpp

OBJS = $(SRCS:.cpp=.o)

TARGET = hyperion

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
