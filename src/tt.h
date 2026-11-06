/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>

  Hyperion is a successor of Gargantua, which is derived from Chess0 and
  inspired by Code Monkey King's bbc-1.4.

  This file is part of Hyperion.

  Hyperion is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Hyperion is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
 * @file tt.h
 * @brief Zobrist keys and the shared transposition table.
 *
 * The table is made of 64-byte cache-line aligned clusters holding four
 * 16-byte entries.  It is shared (lock-free, racy by design) between all Lazy
 * SMP search threads; entries are validated with a 32-bit key fragment.
 */

#ifndef TT_H
#define TT_H

#include "bitboard.h"

#include <cstdint>

// Zobrist hashing keys (filled once by initRandomKeys()).
extern Bitboard piece_keys[12][64];
extern Bitboard enpassant_keys[64];
extern Bitboard castle_keys[16];
extern Bitboard side_key;

inline constexpr int no_hash_found = 100000; ///< Returned by TT::probe() when there is no usable hit.
inline constexpr int ValueNone     = 32001;  ///< Sentinel for "no score / no static eval".

// Bound type of a stored score.
inline constexpr uint8_t hash_type_exact = 0; ///< Score is exact (PV node).
inline constexpr uint8_t hash_type_alpha = 1; ///< Score is an upper bound (fail-low).
inline constexpr uint8_t hash_type_beta  = 2; ///< Score is a lower bound (fail-high).
inline constexpr uint8_t hash_type_none  = 3; ///< Empty slot.

/// One 16-byte transposition table entry.
struct alignas(16) TTEntry_t {
    uint32_t key32;     ///< Upper 32 bits of the position hash.
    int32_t  best_move; ///< Best (or refutation) move, 0 if none.
    int16_t  value;     ///< Search score (mate scores are stored relative to the node).
    int16_t  eval;      ///< Cached raw static evaluation, `ValueNone` if absent.
    int16_t  depth;     ///< Search depth of the stored result.
    uint8_t  gen_bound; ///< bits 0..1: bound, bit 2: PV flag, bits 3..7: generation (0..31).
    uint8_t  pad;

    [[nodiscard]] constexpr uint8_t bound() const noexcept { return gen_bound & 0x3; }
    [[nodiscard]] constexpr bool is_pv() const noexcept { return (gen_bound & 0x4) != 0; }
    [[nodiscard]] constexpr uint8_t gen() const noexcept { return gen_bound >> 3; }

    /// Packs generation, PV flag and bound type into `gen_bound`.
    constexpr void set_gen_bound(uint8_t g, bool pv, uint8_t b) noexcept {
        gen_bound = static_cast<uint8_t>((g << 3) | (pv ? 4 : 0) | (b & 0x3));
    }
};

inline constexpr int ClusterSize = 4; ///< Entries per cluster (one cache line).

/// A cache-line sized bucket of `ClusterSize` entries.
struct alignas(64) TTCluster {
    TTEntry_t entry[ClusterSize];
};

extern uint64_t hash_cluster_count; ///< Number of clusters in the table.
extern TTCluster *hash_table;       ///< The table itself (nullptr until TT::init()).
extern uint8_t tt_generation;       ///< Current search generation (0..31), used for ageing.

/// Fills the Zobrist key tables with a fixed-seed pseudo-random sequence.
void initRandomKeys();

/// @return the Zobrist hash of the current position.
uint64_t generateHashkey();

/// @return the Zobrist hash of the pawn structure only.
uint64_t generatePawnKey();

/// @return the Zobrist hash of the non-pawn pieces of `color`.
uint64_t generateNonPawnKey(int color);

/// Result of a transposition table probe.
struct TTProbeResult {
    bool    hit;     ///< An entry for this position exists.
    bool    cutoff;  ///< The entry allows returning `score` immediately.
    int     score;   ///< Stored score, adjusted to the current ply.
    int     eval;    ///< Cached static evaluation (`ValueNone` if absent).
    int     move;    ///< Stored best move (0 if none).
    int     depth;   ///< Depth of the stored search.
    uint8_t bound;   ///< Bound type of the stored score.
    bool    tt_pv;   ///< The position was on a PV when stored.
};

namespace TT {

/// Empties the whole table and resets the generation counter.
void clear();

/// (Re)allocates the table to `mb` megabytes and clears it.
void init(uint32_t mb);

/// Advances the generation counter; call once at the start of every search.
void new_search() noexcept;

/// Maps a 64-bit key to a cluster index using a 128-bit multiply-high.
[[nodiscard]] inline uint64_t cluster_index(uint64_t key) noexcept {
    return static_cast<uint64_t>(
        (static_cast<unsigned __int128>(key) * static_cast<unsigned __int128>(hash_cluster_count)) >> 64
    );
}

/// Prefetches the cluster of `key` into cache ahead of the probe.
inline void prefetch(uint64_t key) noexcept {
    if (hash_table && hash_cluster_count > 0)
        __builtin_prefetch(&hash_table[cluster_index(key)]);
}

/**
 * Probes the table for the current position (`hash_key`).
 * @param alpha,beta current search window (used to decide `cutoff`)
 * @param depth      required depth for a cutoff
 * @param pv_node    PV nodes never cut off
 */
TTProbeResult probe_ex(int alpha, int beta, int depth, bool pv_node) noexcept;

/// Legacy probe: returns the score on cutoff, `no_hash_found` otherwise, and fills `best_move`.
int probe(int alpha, int beta, int &best_move, int depth) noexcept;

/// Stores a search result for the current position (`hash_key`).
void save(int score, int eval, int best_move, int depth, uint8_t bound, bool is_pv) noexcept;

/// @return the fill ratio of the table in permille, sampled on up to 1000 clusters.
[[nodiscard]] inline int hashfull() noexcept {
    if (hash_cluster_count == 0 || !hash_table)
        return 0;

    int cnt = 0;
    const uint64_t sample_clusters = (hash_cluster_count > 1000) ? 1000 : hash_cluster_count;
    for (uint64_t i = 0; i < sample_clusters; ++i) {
        for (int j = 0; j < ClusterSize; ++j) {
            const TTEntry_t &e = hash_table[i].entry[j];
            if (e.bound() != hash_type_none)
                ++cnt;
        }
    }
    return (cnt * 1000) / int(sample_clusters * ClusterSize);
}

} // namespace TT

#endif // TT_H
