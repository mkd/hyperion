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
 * @file thread.h
 * @brief Per-thread search state and the Lazy SMP thread pool.
 *
 * Every search thread owns a complete ThreadState: a private copy of the
 * board, the history heuristics and the search stacks.  The `current_thread`
 * thread-local pointer selects the active state, and the accessor macros at
 * the bottom of this file (`bitboards`, `ply`, `history`, ...) expand to
 * members of that state.  Only the transposition table is shared.
 */

#ifndef THREAD_H
#define THREAD_H

#include "bitboard.h"
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#ifndef _WIN32
#include <pthread.h>
#else
#include <thread>
#endif
#include <vector>

inline constexpr int MaxPly = 256;                      ///< Maximum search ply (stack size).
inline constexpr int CorrHistSize = 16384;              ///< Entries per correction-history table.
inline constexpr int CorrHistMask = CorrHistSize - 1;   ///< Index mask for correction history.

/// Everything a search thread owns: its board copy, histories and search stacks.
struct ThreadState {
    // Board state
    Bitboard bitboards[12];
    Bitboard occupancies[3];
    int mailbox[64];
    int sideToMove;
    int epsq;
    int castle;
    int fifty;
    int ply;
    uint64_t hash_key;
    uint64_t pawn_key;
    uint64_t non_pawn_key[2];
    bool flip_flag;
    Bitboard repetition_table[1024];
    int repetition_index;
    int root_repetition_index;

    // Search state
    uint64_t node_count;
    int seldepth;
    int killers[2][MaxPly];
    int history[2][64][64];
    int capture_history[12][64][6];
    int cont_history[12][64][12][64];
    int pawn_corrhist[2][CorrHistSize];
    int nonpawn_corrhist[2][2][CorrHistSize];
    int countermoves[12][64];
    int current_move[MaxPly];
    int static_eval_stack[MaxPly];
    int double_extensions[MaxPly];
    int pv_length[MaxPly];
    int pv_table[MaxPly][MaxPly];
    bool followPV;
    bool scorePV;
    bool allowNull;
    int nmp_min_ply;

    /// Zeroes every learned heuristic table (used on `ucinewgame`).
    void clear_histories() noexcept {
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(capture_history, 0, sizeof(capture_history));
        std::memset(cont_history, 0, sizeof(cont_history));
        std::memset(pawn_corrhist, 0, sizeof(pawn_corrhist));
        std::memset(nonpawn_corrhist, 0, sizeof(nonpawn_corrhist));
        std::memset(countermoves, 0, sizeof(countermoves));
    }

    /// Prepares the search stacks for a new search and ages the butterfly history.
    void reset_search_state() noexcept {
        node_count = 0;
        seldepth = 0;
        std::memset(killers, 0, sizeof(killers));
        std::memset(current_move, 0, sizeof(current_move));
        std::memset(static_eval_stack, 0, sizeof(static_eval_stack));
        std::memset(double_extensions, 0, sizeof(double_extensions));
        std::memset(pv_table, 0, sizeof(pv_table));
        std::memset(pv_length, 0, sizeof(pv_length));
        followPV = false;
        scorePV = false;
        allowNull = true;
        nmp_min_ply = 0;
        // Age main butterfly history slightly between moves while preserving learned patterns
        for (int s = 0; s < 2; ++s)
            for (int f = 0; f < 64; ++f)
                for (int t = 0; t < 64; ++t)
                    history[s][f][t] /= 2;
    }

    /// Copies the board (not the heuristics) from `src`, used to sync helper threads to the main thread.
    void copy_board_from(const ThreadState* src) noexcept {
        std::memcpy(bitboards, src->bitboards, sizeof(bitboards));
        std::memcpy(occupancies, src->occupancies, sizeof(occupancies));
        std::memcpy(mailbox, src->mailbox, sizeof(mailbox));
        sideToMove = src->sideToMove;
        epsq = src->epsq;
        castle = src->castle;
        fifty = src->fifty;
        ply = src->ply;
        hash_key = src->hash_key;
        pawn_key = src->pawn_key;
        non_pawn_key[0] = src->non_pawn_key[0];
        non_pawn_key[1] = src->non_pawn_key[1];
        flip_flag = src->flip_flag;
        std::memcpy(repetition_table, src->repetition_table, sizeof(repetition_table));
        repetition_index = src->repetition_index;
        root_repetition_index = src->root_repetition_index;
    }
};

/// A search worker running its own idle loop on a dedicated native thread.
class Thread {
public:
    explicit Thread(size_t n);
    virtual ~Thread();

    /// Iterative-deepening search loop of a helper thread.
    virtual void search();

    /// Thread body: sleeps until woken, syncs NNUE state, searches, then sleeps again.
    void idle_loop();

    /// Wakes the thread to start searching.
    void start_searching();

    /// Blocks until the thread has finished its current search.
    void wait_for_search_finished();

    ThreadState state{};
    size_t idx;
    std::mutex mutex;
    std::condition_variable cv;
#ifndef _WIN32
    pthread_t nativeThread{};
    bool threadStarted = false;
#else
    std::thread stdThread;
#endif
    bool exit = false;
    bool searching = false;

    const void* main_nnue_pos = nullptr;
    const void* main_nnue_setup = nullptr;
    std::atomic<bool> synced{false};
};

/// The thread that owns the UCI position, prints `info` lines and reports `bestmove`.
class MainThread : public Thread {
public:
    explicit MainThread(size_t n) : Thread(n) {}

    /// Iterative deepening with aspiration windows, time management and UCI output.
    void search() override;
};

/// Owns all search threads (index 0 is the MainThread).
class ThreadPool {
public:
    /// Syncs every thread to the main thread's position and starts the search.
    void start_thinking();

    /// Blocks until all threads are idle.
    void wait_for_search_finished();

    /// Destroys all threads.
    void clear();

    /// Clears the learned heuristics of every thread.
    void clear_histories();

    /// Resizes the pool to `n` threads (at least one).
    void set(size_t n);

    /// @return the node count summed over all threads.
    [[nodiscard]] uint64_t nodes_searched() const noexcept;

    std::vector<Thread*> threads;

    MainThread* main() noexcept { return static_cast<MainThread*>(threads.front()); }
};

extern ThreadPool Threads;                               ///< The global thread pool.
extern thread_local ThreadState* current_thread;         ///< State of the thread executing this code.

// ---- Accessors for the active thread's state -------------------------------
// Board state
#define bitboards (current_thread->bitboards)
#define occupancies (current_thread->occupancies)
#define mailbox (current_thread->mailbox)
#define sideToMove (current_thread->sideToMove)
#define epsq (current_thread->epsq)
#define castle (current_thread->castle)
#define fifty (current_thread->fifty)
#define ply (current_thread->ply)
#define hash_key (current_thread->hash_key)
#define pawn_key (current_thread->pawn_key)
#define non_pawn_key (current_thread->non_pawn_key)
#define flip (current_thread->flip_flag)
#define repetition_table (current_thread->repetition_table)
#define repetition_index (current_thread->repetition_index)
#define root_repetition_index (current_thread->root_repetition_index)

// Search state
#define nodes (current_thread->node_count)
#define seldepth (current_thread->seldepth)
#define killers (current_thread->killers)
#define history (current_thread->history)
#define capture_history (current_thread->capture_history)
#define cont_history (current_thread->cont_history)
#define pawn_corrhist (current_thread->pawn_corrhist)
#define nonpawn_corrhist (current_thread->nonpawn_corrhist)
#define countermoves (current_thread->countermoves)
#define current_move (current_thread->current_move)
#define static_eval_stack (current_thread->static_eval_stack)
#define double_extensions (current_thread->double_extensions)
#define pv_length (current_thread->pv_length)
#define pv_table (current_thread->pv_table)
#define followPV (current_thread->followPV)
#define scorePV (current_thread->scorePV)
#define allowNull (current_thread->allowNull)
#define nmp_min_ply (current_thread->nmp_min_ply)

#endif // THREAD_H
