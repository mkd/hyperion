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
 * @file tt.cpp
 * @brief Zobrist key generation and transposition table implementation.
 */

#include "tt.h"
#include "bitboard.h"
#include "position.h"
#include "search.h"
#include <cstddef>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif

Bitboard piece_keys[12][64];
Bitboard enpassant_keys[64];
Bitboard castle_keys[16];
Bitboard side_key;

uint64_t hash_cluster_count = 0ULL;
TTCluster *hash_table = nullptr;
uint8_t tt_generation = 0;

/// Seeds the xorshift generator and fills all Zobrist tables.
void initRandomKeys() {
    rng32_state = 1804289383;
    for (int piece = P; piece <= k; piece++)
        for (int square = 0; square < 64; square++)
            piece_keys[piece][square] = rng64();

    for (int square = 0; square < 64; square++)
        enpassant_keys[square] = rng64();

    for (int index = 0; index < 16; index++)
        castle_keys[index] = rng64();

    side_key = rng64();
}

/// Computes the full position hash from scratch (pieces, en passant, castling, side).
uint64_t generateHashkey() {
    uint64_t final_key = 0ULL;
    for (int piece = P; piece <= k; piece++) {
        Bitboard bb = bitboards[piece];
        while (bb)
            final_key ^= piece_keys[piece][popLsb(bb)];
    }
    if (epsq != NoSq)
        final_key ^= enpassant_keys[epsq];
    final_key ^= castle_keys[castle];
    if (sideToMove == Black)
        final_key ^= side_key;
    return final_key;
}

/// Computes the pawn-structure hash from scratch.
uint64_t generatePawnKey() {
    uint64_t key = 0ULL;
    Bitboard wp = bitboards[P];
    while (wp) key ^= piece_keys[P][popLsb(wp)];
    Bitboard bp = bitboards[p];
    while (bp) key ^= piece_keys[p][popLsb(bp)];
    return key;
}

/// Computes the hash of all non-pawn pieces (kings included) of `color`.
uint64_t generateNonPawnKey(int color) {
    uint64_t key = 0ULL;
    const int start_p = (color == White) ? N : n;
    const int end_p   = (color == White) ? K : k;
    for (int piece = start_p; piece <= end_p; ++piece) {
        Bitboard bb = bitboards[piece];
        while (bb)
            key ^= piece_keys[piece][popLsb(bb)];
    }
    return key;
}

/// Advances the 5-bit generation counter used for age-based replacement.
void TT::new_search() noexcept {
    tt_generation = static_cast<uint8_t>((tt_generation + 1) & 31);
}

/// Resets every entry to the empty state.
void TT::clear() {
    if (!hash_table || hash_cluster_count == 0) return;
    for (uint64_t i = 0; i < hash_cluster_count; ++i) {
        for (int j = 0; j < ClusterSize; ++j) {
            hash_table[i].entry[j].key32 = 0;
            hash_table[i].entry[j].best_move = 0;
            hash_table[i].entry[j].value = 0;
            hash_table[i].entry[j].eval = ValueNone;
            hash_table[i].entry[j].depth = 0;
            hash_table[i].entry[j].gen_bound = hash_type_none;
            hash_table[i].entry[j].pad = 0;
        }
    }
    tt_generation = 0;
}

namespace {

/// Allocates `bytes` of 64-byte aligned memory (portable across POSIX and Windows).
void *allocateAligned(std::size_t bytes) noexcept {
#if defined(_WIN32)
    return _aligned_malloc(bytes, 64);
#else
    void *ptr = nullptr;
    return (posix_memalign(&ptr, 64, bytes) == 0) ? ptr : nullptr;
#endif
}

/// Releases memory obtained from allocateAligned().
void freeAligned(void *ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace

/// Allocates a cache-line aligned table of `mb` MiB (at least 1) and clears it.
void TT::init(uint32_t mb) {
    if (mb < 1)
        mb = 1;

    const uint64_t hash_size = static_cast<uint64_t>(mb) * 1024ULL * 1024ULL;
    hash_cluster_count = hash_size / sizeof(TTCluster);
    if (hash_cluster_count == 0)
        hash_cluster_count = 1;

    if (hash_table != nullptr) {
        freeAligned(hash_table);
        hash_table = nullptr;
    }

    hash_table = static_cast<TTCluster *>(allocateAligned(hash_cluster_count * sizeof(TTCluster)));
    if (hash_table != nullptr)
        TT::clear();
}

/// Looks the current position up in its cluster and refreshes the entry age on a hit.
TTProbeResult TT::probe_ex(int alpha, int beta, int depth, bool pv_node) noexcept {
    TTProbeResult res{false, false, no_hash_found, ValueNone, 0, 0, hash_type_none, false};
    if (!hash_table || hash_cluster_count == 0) return res;

    TTCluster &cluster = hash_table[cluster_index(hash_key)];
    const uint32_t k32 = static_cast<uint32_t>(hash_key >> 32);

    for (int i = 0; i < ClusterSize; ++i) {
        TTEntry_t &e = cluster.entry[i];
        if (e.key32 == k32 && e.bound() != hash_type_none) {
            res.hit = true;
            res.eval = e.eval;
            res.move = e.best_move;
            res.depth = e.depth;
            res.bound = e.bound();
            res.tt_pv = e.is_pv();

            const int score = score_from_tt(e.value, ply, fifty);
            res.score = score;

            // Refresh entry age on hit
            e.set_gen_bound(tt_generation, res.tt_pv, res.bound);

            if (!pv_node && ply > 0 && fifty < 90 && e.depth >= depth) {
                if (res.bound == hash_type_exact) {
                    res.cutoff = true;
                } else if (res.bound == hash_type_alpha && score <= alpha) {
                    res.cutoff = true;
                } else if (res.bound == hash_type_beta && score >= beta) {
                    res.cutoff = true;
                }
            }
            return res;
        }
    }
    return res;
}

/// Convenience wrapper around probe_ex() for callers that only need score and move.
int TT::probe(int alpha, int beta, int &best_move, int depth) noexcept {
    auto res = probe_ex(alpha, beta, depth, false);
    if (res.hit) {
        if (res.move) best_move = res.move;
        if (res.cutoff) return res.score;
    }
    return no_hash_found;
}

/// Stores a result, replacing the lowest-priority entry (depth, age, PV) of the cluster.
void TT::save(int score, int eval, int best_move, int depth, uint8_t bound, bool is_pv) noexcept {
    if (!hash_table || hash_cluster_count == 0) return;

    TTCluster &cluster = hash_table[cluster_index(hash_key)];
    const uint32_t k32 = static_cast<uint32_t>(hash_key >> 32);

    TTEntry_t *replace = &cluster.entry[0];
    int lowest_score = 1000000;

    for (int i = 0; i < ClusterSize; ++i) {
        TTEntry_t &e = cluster.entry[i];
        if (e.key32 == k32 || e.bound() == hash_type_none) {
            replace = &e;
            break;
        }
        int age_diff = (32 + int(tt_generation) - int(e.gen())) & 31;
        int entry_priority = int(e.depth) - age_diff * 4 + (e.is_pv() ? 2 : 0);
        if (entry_priority < lowest_score) {
            lowest_score = entry_priority;
            replace = &e;
        }
    }

    const bool same_pos = (replace->key32 == k32 && replace->bound() != hash_type_none);

    // Preserve existing best move when failing low on the same position
    if (same_pos && best_move == 0) {
        best_move = replace->best_move;
    }
    if (same_pos && eval == ValueNone) {
        eval = replace->eval;
    }

    // Do not overwrite a much deeper entry from the same generation with a shallow non-exact bound
    if (same_pos &&
        bound != hash_type_exact &&
        replace->gen() == tt_generation &&
        depth + 3 + 2 * int(is_pv) < replace->depth) {
        if (replace->best_move == 0 && best_move != 0)
            replace->best_move = best_move;
        return;
    }

    score = score_to_tt(score, ply);

    replace->key32 = k32;
    replace->best_move = best_move;
    replace->value = static_cast<int16_t>(score);
    replace->eval = static_cast<int16_t>(eval);
    replace->depth = static_cast<int16_t>(depth);
    replace->set_gen_bound(tt_generation, is_pv || (same_pos && replace->is_pv()), bound);
}
