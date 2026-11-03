/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef UCI_H
#define UCI_H

#include <chrono>
#include <cstdint>
#include <string>

#define EngineName    "Hyperion"
#define EngineVersion "1.0"
#define EngineAuthor  "Claudio M. Camacho"

inline constexpr uint32_t DefaultHashSize = 64;
inline constexpr uint32_t MinHashSize     = 1;
inline constexpr uint32_t MaxHashSize     = 65536;

extern uint64_t perft_nodes;

void perft_driver(int depth);
void perft_test(int depth);
void bench(int depth = 13);

int parseMove(const std::string &move_str);
void parsePosition(const std::string &command);
void parseGo(const std::string &line);
void uci();

static inline uint64_t getTimeMs() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

#endif // UCI_H
