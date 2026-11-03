/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifndef TT_H
#define TT_H

#include "bitboard.h"
#include <cstdint>

extern Bitboard piece_keys[12][64];
extern Bitboard enpassant_keys[64];
extern Bitboard castle_keys[16];
extern Bitboard side_key;

inline constexpr int no_hash_found   = 100000;
inline constexpr int ValueNone       = 32001;

inline constexpr uint8_t hash_type_exact = 0;
inline constexpr uint8_t hash_type_alpha = 1;
inline constexpr uint8_t hash_type_beta  = 2;
inline constexpr uint8_t hash_type_none  = 3;

struct alignas(16) TTEntry_t {
    uint32_t key32;
    int32_t  best_move;
    int16_t  value;
    int16_t  eval;
    int16_t  depth;
    uint8_t  gen_bound; // bits 0..1: bound, bit 2: pv, bits 3..7: generation (0..31)
    uint8_t  pad;

    constexpr uint8_t bound() const noexcept { return gen_bound & 0x3; }
    constexpr bool is_pv() const noexcept { return (gen_bound & 0x4) != 0; }
    constexpr uint8_t gen() const noexcept { return gen_bound >> 3; }
    constexpr void set_gen_bound(uint8_t g, bool pv, uint8_t b) noexcept {
        gen_bound = static_cast<uint8_t>((g << 3) | (pv ? 4 : 0) | (b & 0x3));
    }
};

inline constexpr int ClusterSize = 4;

struct alignas(64) TTCluster {
    TTEntry_t entry[ClusterSize];
};

extern uint64_t hash_cluster_count;
extern TTCluster *hash_table;
extern uint8_t tt_generation;

void initRandomKeys();
uint64_t generateHashkey();
uint64_t generatePawnKey();
uint64_t generateNonPawnKey(int color);

struct TTProbeResult {
    bool hit;
    bool cutoff;
    int score;
    int eval;
    int move;
    int depth;
    uint8_t bound;
    bool tt_pv;
};

namespace TT {

void clear();
void init(uint32_t mb);
void new_search() noexcept;

static inline uint64_t cluster_index(uint64_t key) noexcept {
    return static_cast<uint64_t>(
        (static_cast<unsigned __int128>(key) * static_cast<unsigned __int128>(hash_cluster_count)) >> 64
    );
}

static inline void prefetch(uint64_t key) noexcept {
    if (hash_table && hash_cluster_count > 0) {
        __builtin_prefetch(&hash_table[cluster_index(key)]);
    }
}

TTProbeResult probe_ex(int alpha, int beta, int depth, bool pv_node) noexcept;
int probe(int alpha, int beta, int &best_move, int depth) noexcept;
void save(int score, int eval, int best_move, int depth, uint8_t bound, bool is_pv) noexcept;

static inline int hashfull() noexcept {
    if (hash_cluster_count == 0 || !hash_table) return 0;
    int cnt = 0;
    const uint64_t sample_clusters = (hash_cluster_count > 250) ? 250 : hash_cluster_count;
    for (uint64_t i = 0; i < sample_clusters; ++i) {
        for (int j = 0; j < ClusterSize; ++j) {
            if (hash_table[i].entry[j].bound() != hash_type_none &&
                hash_table[i].entry[j].gen() == tt_generation) {
                ++cnt;
            }
        }
    }
    return (cnt * 1000) / int(sample_clusters * ClusterSize);
}

} // namespace TT

#endif // TT_H
