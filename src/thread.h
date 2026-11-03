/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
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

inline constexpr int MaxPly = 256;
inline constexpr int CorrHistSize = 16384;
inline constexpr int CorrHistMask = CorrHistSize - 1;

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

    void clear_histories() noexcept {
        std::memset(killers, 0, sizeof(killers));
        std::memset(history, 0, sizeof(history));
        std::memset(capture_history, 0, sizeof(capture_history));
        std::memset(cont_history, 0, sizeof(cont_history));
        std::memset(pawn_corrhist, 0, sizeof(pawn_corrhist));
        std::memset(nonpawn_corrhist, 0, sizeof(nonpawn_corrhist));
        std::memset(countermoves, 0, sizeof(countermoves));
    }

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

class Thread {
public:
    explicit Thread(size_t n);
    virtual ~Thread();

    virtual void search();
    void idle_loop();
    void start_searching();
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

class MainThread : public Thread {
public:
    explicit MainThread(size_t n) : Thread(n) {}
    void search() override;
};

class ThreadPool {
public:
    void start_thinking();
    void wait_for_search_finished();
    void clear();
    void clear_histories();
    void set(size_t n);
    uint64_t nodes_searched() const noexcept;

    std::vector<Thread*> threads;

    MainThread* main() noexcept { return static_cast<MainThread*>(threads.front()); }
};

extern ThreadPool Threads;
extern thread_local ThreadState* current_thread;

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
