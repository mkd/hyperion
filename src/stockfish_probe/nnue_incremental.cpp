#include "nnue_incremental.h"
#include "probe.h"
#include "evaluate.h"
#include "position.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <iostream>

namespace Stockfish {

extern Eval::NNUE::Network global_network;

namespace {

constexpr int getMoveSource(int move)  { return move & 0x3f; }
constexpr int getMoveTarget(int move)  { return (move & 0xfc0) >> 6; }
constexpr int getMovePiece(int move)   { return (move & 0xf000) >> 12; }
constexpr int getPromo(int move)       { return (move & 0xf0000) >> 16; }
constexpr int getEp(int move)          { return move & 0x400000; }
constexpr int getCastle(int move)      { return move & 0x800000; }

constexpr int mapSquare(int sq) { return sq ^ 56; }

Move convert_move(int move) {
    const int from = getMoveSource(move);
    const int to = getMoveTarget(move);
    const int promo_idx = getPromo(move);

    const Square sf_from = Square(mapSquare(from));
    Square sf_to = Square(mapSquare(to));

    if (promo_idx != 0) {
        PieceType promo_pt;
        switch (promo_idx) {
            case 1: case 7: promo_pt = KNIGHT; break;
            case 2: case 8: promo_pt = BISHOP; break;
            case 3: case 9: promo_pt = ROOK;   break;
            default:        promo_pt = QUEEN;  break;
        }
        return Move::make<PROMOTION>(sf_from, sf_to, promo_pt);
    } else if (getCastle(move)) {
        if (sf_to == SQ_G1)      sf_to = SQ_H1;
        else if (sf_to == SQ_C1) sf_to = SQ_A1;
        else if (sf_to == SQ_G8) sf_to = SQ_H8;
        else if (sf_to == SQ_C8) sf_to = SQ_A8;
        return Move::make<CASTLING>(sf_from, sf_to);
    } else if (getEp(move)) {
        return Move::make<EN_PASSANT>(sf_from, sf_to);
    } else {
        return Move::make<NORMAL>(sf_from, sf_to);
    }
}

struct ThreadData {
    Position pos;
    std::vector<StateInfo> search_stack;
    size_t stack_ptr = 0;
    std::deque<StateInfo> setup_states;
    Eval::NNUE::AccumulatorStack accumulator_stack;
    std::unique_ptr<Eval::NNUE::AccumulatorCaches> accumulator_caches;

    ThreadData() : search_stack(2048) {
        accumulator_stack.reset();
    }

    Eval::NNUE::AccumulatorCaches& caches() {
        if (!accumulator_caches) {
            accumulator_caches = std::make_unique<Eval::NNUE::AccumulatorCaches>(global_network);
        }
        return *accumulator_caches;
    }
};

thread_local ThreadData thread_data;

} // namespace

namespace Incremental {

void init() {
    thread_data.search_stack.resize(2048);
    thread_data.stack_ptr = 0;
    setup_reset("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void push_state(int move) {
    if (thread_data.stack_ptr >= thread_data.search_stack.size()) {
        thread_data.search_stack.resize(thread_data.search_stack.size() * 2);
    }
    StateInfo* next_si = &thread_data.search_stack[thread_data.stack_ptr++];
    Dirties& dirties = thread_data.accumulator_stack.push();
    const Move sf_move = convert_move(move);
    const bool gives_check = thread_data.pos.gives_check(sf_move);
    thread_data.pos.do_move(sf_move, *next_si, gives_check, dirties, nullptr, nullptr);
}

void push_null_state() {
    if (thread_data.stack_ptr >= thread_data.search_stack.size()) {
        thread_data.search_stack.resize(thread_data.search_stack.size() * 2);
    }
    StateInfo* next_si = &thread_data.search_stack[thread_data.stack_ptr++];
    thread_data.pos.do_null_move(*next_si);
}

void pop_state(int move) {
    if (move == 0) {
        thread_data.pos.undo_null_move();
    } else {
        const Move sf_move = convert_move(move);
        thread_data.pos.undo_move(sf_move);
        thread_data.accumulator_stack.pop();
    }
    --thread_data.stack_ptr;
}

void setup_reset(const std::string &fen) {
    thread_data.setup_states.clear();
    thread_data.setup_states.emplace_back();
    thread_data.pos.set(fen, false, &thread_data.setup_states.back());
    thread_data.accumulator_stack.reset();
    thread_data.caches().clear(global_network);
    thread_data.stack_ptr = 0;
}

void setup_move(int move) {
    thread_data.setup_states.emplace_back();
    StateInfo* next_si = &thread_data.setup_states.back();
    Dirties& dirties = thread_data.accumulator_stack.push();
    const Move sf_move = convert_move(move);
    const bool gives_check = thread_data.pos.gives_check(sf_move);
    thread_data.pos.do_move(sf_move, *next_si, gives_check, dirties, nullptr, nullptr);
}

void setup_undo(int move) {
    const Move sf_move = convert_move(move);
    thread_data.pos.undo_move(sf_move);
    thread_data.accumulator_stack.pop();
    thread_data.setup_states.pop_back();
}

int evaluate() {
    return Eval::evaluate(global_network, thread_data.pos,
                          thread_data.accumulator_stack, thread_data.caches(), 0);
}

void hint_common_parent() {
    // Automatically handled by AccumulatorStack in Stockfish 19
}

const void* get_global_pos() {
    return &thread_data.pos;
}

const void* get_setup_states() {
    return &thread_data.setup_states;
}

void sync_from_main_thread(const void* main_pos, const void* main_setup) {
    if (main_setup) {
        thread_data.setup_states = *static_cast<const std::deque<StateInfo>*>(main_setup);
    }
    if (main_pos) {
        std::memcpy(&thread_data.pos, main_pos, sizeof(Position));
        if (!thread_data.setup_states.empty()) {
            thread_data.pos.set_state(&thread_data.setup_states.back());
            for (size_t i = 0; i < thread_data.setup_states.size(); ++i) {
                thread_data.setup_states[i].previous = (i == 0) ? nullptr : &thread_data.setup_states[i - 1];
            }
        } else {
            thread_data.pos.set_state(nullptr);
        }
    }
    thread_data.search_stack.resize(2048);
    thread_data.stack_ptr = 0;
    thread_data.accumulator_stack.reset();
    thread_data.caches().clear(global_network);
}

} // namespace Incremental
} // namespace Stockfish
