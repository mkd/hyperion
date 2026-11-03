/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho
*/

#ifdef USE_NEW_NNUE
#include "stockfish_probe/nnue_incremental.h"
#endif
#include "search.h"
#include "thread.h"
#include <cassert>

#undef repetition_index
#undef root_repetition_index

ThreadPool Threads;
thread_local ThreadState* current_thread = nullptr;

Thread::Thread(size_t n) : idx(n) {
    state.clear_histories();
    state.reset_search_state();
#ifndef _WIN32
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024); // 16 MB stack per thread (macOS default is only 512 KB)
    auto start_fn = +[](void* arg) -> void* {
        static_cast<Thread*>(arg)->idle_loop();
        return nullptr;
    };
    if (pthread_create(&nativeThread, &attr, start_fn, this) == 0) {
        threadStarted = true;
    }
    pthread_attr_destroy(&attr);
#else
    stdThread = std::thread(&Thread::idle_loop, this);
#endif
}

Thread::~Thread() {
    {
        std::unique_lock<std::mutex> lk(mutex);
        exit = true;
        cv.notify_one();
    }
#ifndef _WIN32
    if (threadStarted) {
        pthread_join(nativeThread, nullptr);
    }
#else
    if (stdThread.joinable()) {
        stdThread.join();
    }
#endif
}

void Thread::idle_loop() {
    current_thread = &state;

    while (true) {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [&] { return searching || exit; });

        if (exit) {
            return;
        }

        lk.unlock();

#ifdef USE_NEW_NNUE
        if (main_nnue_pos && main_nnue_setup) {
            Stockfish::Incremental::sync_from_main_thread(
                *static_cast<const Stockfish::Position*>(main_nnue_pos),
                *static_cast<const std::deque<Stockfish::StateInfo>*>(main_nnue_setup)
            );
        }
#endif

        synced.store(true, std::memory_order_release);

        search();
        lk.lock();

        searching = false;
        cv.notify_one();
    }
}

void Thread::start_searching() {
    std::unique_lock<std::mutex> lk(mutex);
    searching = true;
    cv.notify_one();
}

void Thread::wait_for_search_finished() {
    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [&] { return !searching; });
}

void ThreadPool::start_thinking() {
#ifdef USE_NEW_NNUE
    const void* main_pos = &Stockfish::Incremental::get_global_pos();
    const void* main_setup = &Stockfish::Incremental::get_setup_states();
#else
    const void* main_pos = nullptr;
    const void* main_setup = nullptr;
#endif

    main()->state.root_repetition_index = main()->state.repetition_index;

    for (size_t i = 0; i < threads.size(); i++) {
        if (i > 0) {
            threads[i]->state.copy_board_from(&main()->state);
        }
        threads[i]->state.reset_search_state();
        threads[i]->main_nnue_pos = main_pos;
        threads[i]->main_nnue_setup = main_setup;
        threads[i]->synced.store(false, std::memory_order_relaxed);
    }

    for (size_t i = 1; i < threads.size(); i++) {
        threads[i]->start_searching();
    }

    main()->start_searching();

    for (size_t i = 0; i < threads.size(); i++) {
        while (!threads[i]->synced.load(std::memory_order_acquire)) {}
    }
}

void ThreadPool::clear() {
    for (Thread* th : threads) {
        delete th;
    }
    threads.clear();
}

void ThreadPool::clear_histories() {
    for (Thread* th : threads) {
        th->state.clear_histories();
    }
}

void ThreadPool::set(size_t n) {
    if (n == 0) n = 1;
    if (threads.empty()) {
        threads.push_back(new MainThread(0));
        current_thread = &threads.front()->state;
    }
    while (threads.size() < n) {
        threads.push_back(new Thread(threads.size()));
    }
    while (threads.size() > n) {
        delete threads.back();
        threads.pop_back();
    }
    current_thread = &threads.front()->state;
}

uint64_t ThreadPool::nodes_searched() const noexcept {
    uint64_t sum = 0;
    for (Thread* th : threads) {
        sum += th->state.node_count;
    }
    return sum;
}

void ThreadPool::wait_for_search_finished() {
    for (Thread* th : threads) {
        th->wait_for_search_finished();
    }
}
