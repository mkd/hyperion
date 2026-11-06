# Hyperion 1.0 — A Modern C++26 UCI Chess Engine

**Hyperion** is a high-performance C++26 UCI chess engine designed as the next-generation successor to Gargantua. It combines Stockfish SF16.1 dual-network NNUE evaluation (`HalfKAv2_hm`, Big 2560 + Small 128) with a modern alpha-beta Principal Variation Search (PVS) architecture, cache-aligned 4-way bucketed transposition tables, incremental `mailbox[64]` and pawn/non-pawn Zobrist keys, staged move ordering, and multi-table history + correction history heuristics.

---

## Key Architectural & Algorithmic Improvements over Gargantua

1. **C++26 Modernizations & Hardware Bit Intrinsics (`src/bitboard.h`, `src/position.h`)**:
   - Compiled with `-std=c++26 -O3 -flto` (`clang++`).
   - Uses `consteval` compile-time lambdas for lookup tables (`SqBB`), hardware `std::popcount` and `std::countr_zero` (`<bit>`), and `constexpr` piece/character mapping tables (eliminating `std::map` heap allocations in board setup and move formatting).

2. **`O(1)` Incremental `mailbox[64]` & Pawn/Non-Pawn Zobrist Keys (`src/position.cpp`, `src/movgen.h`)**:
   - Maintains an incremental 64-square `mailbox[64]` array alongside bitboards, turning captured-piece lookups in `makeMove()`, `scoreMove()`, `see()`, and `getFEN()` from `O(12)` bitboard loops into `O(1)` array reads.
   - Incrementally maintains `pawn_key` and `non_pawn_key[2]` Zobrist hashes on every move for `O(1)` Correction History indexing.
   - Fixes the 50-move rule reset bug on pawn pushes (`fifty = 0`) and bounds repetition detection by the 50-move counter (`std::max(0, repetition_index - fifty)` stepping by 2 plies).

3. **64-Byte Cache-Line Aligned 4-Way Bucketed Transposition Table (`src/tt.h`, `src/tt.cpp`)**:
   - Replaces Gargantua's unaligned 25-byte single-slot `TTEntry_t` with a 64-byte cache-line aligned `TTCluster` containing four 16-byte entries (`key32`, `best_move`, `value`, static `eval`, `depth`, and packed `gen_bound` with PV flag and 5-bit generation counter).
   - Uses 128-bit multiply-high indexing (`(uint128_t(key) * cluster_count) >> 64`), `__builtin_prefetch`, static evaluation caching in TT entries, and smart age/depth/PV replacement that preserves `best_move` on fail-low (`hash_type_alpha`) stores.

4. **Lazy King-Bucket NNUE Accumulator Refresh & Parent Hinting (`src/stockfish_probe/nnue_incremental.cpp`)**:
   - Removed Gargantua's eager `if (type_of(sf_piece) == KING) evaluate();` call inside `do_move()`, which previously forced a full NNUE network evaluation on every King move even when the node was immediately pruned or cut off without ever calling `evaluate()`.
   - Added `Stockfish::Incremental::hint_common_parent()` before the move loop so child nodes share a pre-computed parent accumulator.
   - Defers `NNUE_DO(move)` until *after* `makeMove(move)` confirms legality and *after* all pre-move pruning gates pass.

5. **Modern PVS Search & Pre-Move Pruning Pipeline (`src/search.cpp`)**:
   - **Pre-Move Pruning (before `makeMove` and `NNUE_DO`)**:
     - Late Move Pruning (LMP) modulated by the `improving` heuristic.
     - Futility Pruning using LMR-adjusted depth and quiet history scores.
     - Quiet History Pruning and SEE Pruning for both quiet moves and captures (`see_ge` with `O(1)` upper/lower-bound fast paths).
   - **Fixed & Enhanced Node Pruning/Extensions**:
     - **Null Move Pruning**: Properly gated on `eval >= beta` and `static_eval >= beta - 20 * depth + 170`, with high-depth Zugzwang verification search.
     - **Singular Extensions**: Properly applies `extension` to the first move (`new_depth = depth - 1 + extension`), supports Double Extensions (`extension = 2`), Multi-Cut Pruning (`sBeta >= beta`), and Negative Extensions (`extension = -1`), and protects the PV table during singular searches.
     - **ProbCut**: Generates and searches good captures (`see_ge(move, probBeta - static_eval)`) with a `qsearch` pre-filter followed by a `depth - 4` verification search.
     - **Internal Iterative Reductions (IIR)**: Reduces depth by 1 on PV and cut nodes when no TT move is available (`depth >= 4`).

6. **Staged Move Ordering, Multi-Table History & Pawn-Structure History (`src/search.cpp`, `src/eval.h`, `src/thread.h`)**:
   - Orders moves as: `PV/TT Move` -> `Good Captures & Promotions (SEE >= -combined_cap_hist/32)` -> `Killer 1` -> `Killer 2` -> `Countermove` -> `Quiet Moves (Butterfly + 1-ply, 2-ply & 4-ply Continuation History + Pawn Structure History)` -> `Bad Captures (SEE < 0)`.
   - Uses partial selection sort (`pickNextMove`) on demand rather than sorting all moves upfront.
   - **Multi-Ply Continuation History**: Separated 1-ply (opponent response), 2-ply (self follow-up), and 4-ply tables (`cont_history[3][12][64][12][64]`).
   - **Capture Continuation History**: 1-ply tactical recapture history (`capture_cont_history[12][64][12][64]`) prioritizing sharp recaptures provoked by the opponent's previous move.
   - **Pawn-Structure History**: Quiet history indexed by Zobrist pawn hash (`pawn_history[1024][12][64]`), learning positional piece placements for specific pawn structures.
   - Updates Butterfly History, Continuation Histories, Capture Histories, and Pawn History with Stockfish gravity (`bonus` for cutoff move, `-bonus` malus for previously tried moves).
   - Adjusts raw NNUE static evaluations using Pawn and Non-Pawn **Correction History** (`pawn_corrhist`, `nonpawn_corrhist`).

7. **MultiPV Analysis & Soft/Hard Time Management (`src/uci.cpp`, `src/search.cpp`)**:
   - **MultiPV UCI Analysis Mode**: Full support for `setoption name MultiPV value <N>` (1..256), extracting, ranking, and printing the top $K$ principal variations at root with TT safety guards and descending score ordering.
   - Soft (`opt_stoptime`) and hard (`max_stoptime`) bounds dynamically scaled by **Best-Move Stability** and **Eval-Drop** heuristics.
   - Runs a clean, non-blocking UCI loop without race conditions on `std::cin`.

8. **Syzygy Endgame Tablebases & DTZ Probing (`src/search.cpp`, `src/tbprobe.cpp`, `src/tbconfig.h`, `src/uci.cpp`)**:
   - **Root Syzygy DTZ Probing**: Uses Distance-to-Zero (`.rtbz`) tablebases at root to find the fastest zeroing move (pawn push, capture, or mate) under the 50-move rule, avoiding 50-move draws in theoretical wins and securing instantaneous (< 1ms) responses.
   - **MultiPV DTZ Support**: Ranks all legal root moves by DTZ/WDL score and outputs multiple principal variations under `setoption name MultiPV value <N>`.
   - **50-Move Proximity Awareness**: Distinguishes full wins (`TB_WIN`), cursed wins (`TB_CURSED_WIN`, reported as `cp 1`), blessed losses (`TB_BLESSED_LOSS`, reported as `cp -1`), draws (`TB_DRAW`), and direct mates (`mate 1`).
   - **Auto-Discovery**: Automatically discovers Syzygy tablebases in `../syzygy`, `./syzygy`, or `syzygy` at engine startup, configurable via `setoption name SyzygyPath value <path>`.
   - **In-Tree WDL Probing**: Probes Win-Draw-Loss (`.rtbw`) tablebases during alpha-beta search (`ply > 0`) to prune and cut off won/lost/drawn branches.

---

## Building & Running

### Native build
```bash
make -j
./hyperion bench 12
# Or simply:
make bench
```

### Cross-compilation
```bash
# Build for a specific target into dist/
make TARGET=windows-x86_64
make TARGET=linux-x86_64
make TARGET=linux-arm64
make TARGET=macos-x86_64

# Build every target with an available toolchain
make release
```

---

## License

Hyperion is licensed under the [GNU General Public License v3.0 or later](LICENSE).
Author: Claudio M. Camacho <claudiomkd@gmail.com>
