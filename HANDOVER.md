# Hyperion 1.0 — Project Handover Document

## 1. Project Overview

**Hyperion** (`/Users/claudio/projects/Chess/hyperion`) is a standalone C++26 UCI chess engine created as a ground-up architectural and algorithmic successor to **Gargantua 2.1**.

- **Language & Standard**: C++26 (`-std=c++26 -O3 -flto -fno-exceptions -DNDEBUG`)
- **Evaluation**: Dual-network Stockfish SF16.1 NNUE (`HalfKAv2_hm`, Big `2560` `nn-b1a57edbea57.nnue` + Small `128` `nn-baff1ede1f90.nnue`), augmented with Pawn & Non-Pawn **Correction History**.
- **Parallel Search**: Lazy SMP (`Threads` 1..1024) with 16 MB per-thread `pthread` stacks on macOS/POSIX.
- **Endgame Tablebases**: Fathom Syzygy WDL probing (`SyzygyPath` UCI option).

---

## 2. Directory & File Structure

```text
/Users/claudio/projects/Chess/hyperion/
├── Makefile                        # Top-level Makefile (`make -j8`, `make clean`)
├── README.md                       # Architecture & usage overview
├── HANDOVER.md                     # This handover guide for new Antigravity sessions
├── .gitignore                      # Ignores object files and compiled binaries
├── nn-b1a57edbea57.nnue            # Symlink -> src/nn-b1a57edbea57.nnue
├── nn-baff1ede1f90.nnue            # Symlink -> src/nn-baff1ede1f90.nnue
└── src/
    ├── Makefile                    # C++26 build rules with ARM64 NEON / x86_64 AVX2 detection
    ├── main.cpp                    # Engine initialization, CLI `bench [depth]` & `uci()` entry
    ├── bitboard.h / bitboard.cpp   # C++26 `consteval SqBB`, `std::popcount`, `std::countr_zero`, magic bitboards
    ├── position.h / position.cpp   # Board representation, incremental `mailbox[64]`, `pawn_key`, `non_pawn_key[2]`, fast draw/repetition detection
    ├── movgen.h / movgen.cpp       # Pseudo-legal move generators (`generateMoves`, `generateCapturesAndPromotions`, `generateQuietMoves`) & `makeMove`
    ├── tt.h / tt.cpp               # 64-byte cache-aligned 4-way bucketed `TTCluster` (16B `TTEntry_t`), 128-bit indexing, prefetch, static eval caching
    ├── thread.h / thread.cpp       # Lazy SMP `ThreadPool`, 16 MB `pthread` stacks, `ThreadState` with history & correction history tables
    ├── eval.h / eval.cpp           # NNUE evaluation wrapper, path auto-discovery, Pawn & Non-Pawn Correction History adjustment/update
    ├── search.h / search.cpp       # PVS alpha-beta search, Quiescence, SEE (`see` & `see_ge`), staged move picker, pruning/extensions, soft/hard time management
    ├── uci.h / uci.cpp             # Non-blocking UCI loop, `bench`, `perft`, `eval`, `d`, `flip`
    ├── tbprobe.h / tbprobe.cpp     # Syzygy endgame tablebase probing (Fathom)
    ├── tbchess.c / tbconfig.h / stdendian.h
    ├── crash_recovery.h / crash_recovery.cpp
    ├── nn-b1a57edbea57.nnue        # Big NNUE network (64.5 MB)
    ├── nn-baff1ede1f90.nnue        # Small NNUE network (3.6 MB)
    └── stockfish_probe/            # Incremental SF16.1 NNUE accumulator engine
        ├── nnue_incremental.h / nnue_incremental.cpp
        ├── evaluate.h / evaluate.cpp
        ├── position.h / position.cpp
        ├── probe.h / probe.cpp
        ├── bitboard.h / bitboard.cpp / misc.h / misc.cpp / memory.h / memory.cpp / types.h
        └── nnue/                   # HalfKAv2_hm feature transformer, SIMD layers, accumulator
```

---

## 3. Architectural & Algorithmic Improvements Implemented

### A. Board Representation & Move Generation (`bitboard.h`, `position.h`, `movgen.h`)
1. **C++26 `consteval` & `<bit>` Intrinsics**:
   - `SqBB` is generated at compile time via a C++26 `consteval` lambda.
   - `countBits` and `ls1b` / `popLsb` use `constexpr std::popcount` and `std::countr_zero`.
   - Replaced `std::map` piece/character lookups with `constexpr` switch tables (`charToPiece`, `promoChar`).
2. **Incremental `mailbox[64]`**:
   - Maintained incrementally in `setPosition` and `makeMove`.
   - Eliminates `O(12)` bitboard loops on captures in `makeMove()`, `scoreMove()`, `see()`, `see_ge()`, and `getFEN()`.
3. **Incremental `pawn_key` & `non_pawn_key[2]`**:
   - Updated incrementally in `makeMove()` for `O(1)` Correction History table lookups.
4. **Fixed 50-Move Rule & Bounded Repetition Detection**:
   - Fixed Gargantua's bug where pawn pushes did not reset `fifty = 0`.
   - `isRepetition()` only scans back `std::max(0, repetition_index - fifty)` stepping by 2 plies (`index -= 2`), returning `true` on 2-fold repetition inside the search tree (`index >= root_repetition_index`) or 3-fold including game history.
5. **Fast `isSquareAttacked`**:
   - Combines `B | Q` and `R | Q` before looking up bishop/rook magic attacks, halving slider attack lookups on every legality check.

### B. 64-Byte Cache-Line Aligned 4-Way Bucketed TT (`tt.h`, `tt.cpp`)
- Replaces Gargantua's 25-byte unaligned single-entry TT with `alignas(64) TTCluster` containing 4 x 16-byte `TTEntry_t` entries (`key32`, `best_move`, `value`, static `eval`, `depth`, and `gen_bound` packing bound + PV flag + 5-bit generation age).
- Uses 128-bit multiply-high indexing (`(uint128_t(key) * hash_cluster_count) >> 64`) and `__builtin_prefetch`.
- Preserves existing `best_move` when overwriting a same-position entry with a fail-low (`hash_type_alpha`) result.

### C. Incremental NNUE Optimizations (`stockfish_probe/nnue_incremental.cpp`, `eval.cpp`)
- Removed Gargantua's eager `if (type_of(sf_piece) == KING) evaluate();` inside `do_move()`, which previously forced a full NNUE evaluation on every King move even when pruned or cut off before `evaluate()`.
- Added `Stockfish::Incremental::hint_common_parent()` before the move loop in `negamax` so sibling moves share an updated parent accumulator.
- Deferred `NNUE_DO(move)` until *after* all pre-move pruning gates pass and `makeMove(move)` confirms legality.
- Removed dead bitboard loops and double 50-move rule damping from `eval.cpp`.
- Added automatic `.nnue` file resolution relative to `argv[0]` (`std::filesystem`) so Hyperion can be invoked from any working directory.

### D. Search, Pruning, Move Ordering & History (`search.cpp`, `eval.h`, `thread.h`)
- **Pre-Move Pruning (before `makeMove` and `NNUE_DO`)**:
  - Late Move Pruning (LMP) scaled by `improving`.
  - Futility Pruning using LMR depth and quiet history.
  - Quiet History Pruning and Quiet/Capture SEE Pruning (`see_ge` with `O(1)` upper/lower-bound fast paths).
- **Node-Level Pruning & Extensions**:
  - **Null Move Pruning**: Properly requires `eval >= beta` and `static_eval >= beta - 20 * depth + 170`, plus high-depth verification search (`depth >= 14`).
  - **Singular Extensions**: Properly applies `extension` to the first move (`new_depth = depth - 1 + extension`), supports Double Extensions (`extension = 2`), Multi-Cut (`sBeta >= beta`), and Negative Extensions (`extension = -1`), and protects `pv_length` / `pv_table` when `excludedMove != 0`.
  - **ProbCut**: Searches good captures (`see_ge(move, probBeta - static_eval)`) with a `qsearch` pre-filter followed by a `depth - 4` search.
  - **Internal Iterative Reductions (IIR)**: Reduces `depth--` on PV and cut nodes (`depth >= 4`) when `!bestmove`.
- **Move Ordering & Histories**:
  - Staged selection sort (`pickNextMove`) ordering: `PV Move` -> `TT Move` -> `Good Captures & Promotions (SEE >= -cap_hist/32)` -> `Killer 1` -> `Killer 2` -> `Countermove` -> `Quiet Moves (Butterfly + 1-ply & 2-ply Continuation History)` -> `Bad Captures (SEE < 0)`.
  - Gravity updates (`bonus` and `-bonus` malus) for Butterfly History (`history[2][64][64]`), 1-ply & 2-ply Continuation History (`cont_history[12][64][12][64]`), and Capture History (`capture_history[12][64][6]`).
  - Pawn & Non-Pawn **Correction History** (`pawn_corrhist[2][16384]`, `nonpawn_corrhist[2][2][16384]`).
- **Time Management & Threading**:
  - Soft (`opt_stoptime`) and hard (`max_stoptime`) time limits scaled by Best-Move Stability (`0.65x .. 1.45x`) and Eval-Drop (`1.0x .. 1.5x`).
  - 16 MB `pthread` stacks (`pthread_attr_setstacksize`) on macOS/POSIX so deep multi-threaded searches (`seldepth 90+`) never overflow macOS's 512 KB default pthread stack.

---

## 4. Benchmarks & Match Verification vs Gargantua 2.1

- **`./src/hyperion bench 12` (16 positions, 1 thread)**: `750,986` nodes in `545 ms` (**~1.38M – 1.64M NPS** single-threaded).
- **8-Thread Tactical Search**: Reaches **25M – 30.3M NPS** and solves `batch_positions.py` Position 1 (`M7`) at depth 32 in `952 ms` (continuing cleanly to depth 48+).
- **Head-to-Head Matches vs Gargantua 2.1**:
  - **10 games @ `movetime = 0.1s/move`**: **10.0 / 10 (`+10 -0 =0`, 100% win rate)**
  - **10 games @ `depth = 8`**: **10.0 / 10 (`+10 -0 =0`, 100% win rate)**

---

## 5. Potential Next Steps for Future Sessions

1. **MultiPV Support**: Add a `MultiPV` UCI option for analysis tools.
2. **4-Ply Continuation History & Pawn History**: Extend continuation history to `-4` plies and add a pawn-structure-indexed quiet history table (`pawn_history[2][512][12][64]`).
3. **SPSA Parameter Tuning**: Tune LMR, RFP, Futility, and Correction History weights via OpenBench / cutechess-cli SPRT testing.
4. **Root Syzygy DTZ Probing**: Add `tb_probe_root` DTZ probing at the root alongside in-tree `tb_probe_wdl`.
