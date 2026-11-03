#include "nnue_incremental.h"
#include "evaluate.h"
#include "nnue/evaluate_nnue.h"
#include "probe.h"
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

#define getMoveSource(move) ((move) & 0x3f)
#define getMoveTarget(move) (((move) & 0xfc0) >> 6)
#define getMovePiece(move) (((move) & 0xf000) >> 12)
#define getPromo(move) (((move) & 0xf0000) >> 16)
#define getMoveCapture(move) ((move) & 0x100000)
#define getDoublePush(move) ((move) & 0x200000)
#define getEp(move) ((move) & 0x400000)
#define getCastle(move) ((move) & 0x800000)

#define MAP_SQ(sq) ((sq) ^ 56)

namespace Stockfish {
namespace Incremental {

thread_local Position global_pos;
thread_local StateInfo global_si;
thread_local std::vector<StateInfo> search_stack;
thread_local size_t stack_ptr = 0;
thread_local std::deque<StateInfo> setup_states;

static constexpr int g_to_sf_piece[12] = {
    1, 2,  3,  4,  5,  6,  // P N B R Q K -> W_PAWN..W_KING
    9, 10, 11, 12, 13, 14  // p n b r q k -> B_PAWN..B_KING
};

inline Piece map_piece(int g_piece) {
    return (g_piece >= 0 && g_piece <= 11) ? Piece(g_to_sf_piece[g_piece]) : NO_PIECE;
}

void init() {
    setup_reset("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    search_stack.resize(2048);
    stack_ptr = 0;
}

void push_state(int move) {
    StateInfo *si = &search_stack[stack_ptr++];
    do_move(move, si);
}

void push_null_state() {
    StateInfo *si = &search_stack[stack_ptr++];
    do_null_move(si);
}

void pop_state(int move) {
    undo_move(move);
    --stack_ptr;
}

void do_move(int move, StateInfo *new_si) {
    new_si->previous = global_pos.state();

    new_si->accumulatorBig.computed[WHITE] = false;
    new_si->accumulatorBig.computed[BLACK] = false;
    new_si->accumulatorSmall.computed[WHITE] = false;
    new_si->accumulatorSmall.computed[BLACK] = false;

    const int from = getMoveSource(move);
    const int to = getMoveTarget(move);
    const int piece_idx = getMovePiece(move);
    const int promo_idx = getPromo(move);

    const Square sf_from = Square(MAP_SQ(from));
    const Square sf_to = Square(MAP_SQ(to));
    const Piece sf_piece = map_piece(piece_idx);
    const Piece sf_promo = map_piece(promo_idx);

    DirtyPiece &dp = new_si->dirtyPiece;
    dp.dirty_num = 0;

    Piece captured = global_pos.piece_on(sf_to);

    new_si->nonPawnMaterial[WHITE] = new_si->previous->nonPawnMaterial[WHITE];
    new_si->nonPawnMaterial[BLACK] = new_si->previous->nonPawnMaterial[BLACK];

    if (getEp(move)) {
        const Square cap_sq = make_square(file_of(sf_to), rank_of(sf_from));
        captured = global_pos.piece_on(cap_sq);
        global_pos.remove_piece(cap_sq);
        dp.piece[dp.dirty_num] = captured;
        dp.from[dp.dirty_num] = cap_sq;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;
    } else if (captured != NO_PIECE) {
        if (type_of(captured) != PAWN) {
            new_si->nonPawnMaterial[color_of(captured)] -= PieceValue[captured];
        }
        global_pos.remove_piece(sf_to);
        dp.piece[dp.dirty_num] = captured;
        dp.from[dp.dirty_num] = sf_to;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;
    }

    if (promo_idx != 0) {
        global_pos.remove_piece(sf_from);
        dp.piece[dp.dirty_num] = sf_piece;
        dp.from[dp.dirty_num] = sf_from;
        dp.to[dp.dirty_num] = SQ_NONE;
        dp.dirty_num++;

        global_pos.put_piece(sf_promo, sf_to);
        dp.piece[dp.dirty_num] = sf_promo;
        dp.from[dp.dirty_num] = SQ_NONE;
        dp.to[dp.dirty_num] = sf_to;
        dp.dirty_num++;

        if (type_of(sf_promo) != PAWN) {
            new_si->nonPawnMaterial[color_of(sf_promo)] += PieceValue[sf_promo];
        }
    } else {
        global_pos.remove_piece(sf_from);
        if (getCastle(move)) {
            Square r_from = SQ_NONE;
            Square r_to = SQ_NONE;
            if (sf_to == SQ_G1)      { r_from = SQ_H1; r_to = SQ_F1; }
            else if (sf_to == SQ_C1) { r_from = SQ_A1; r_to = SQ_D1; }
            else if (sf_to == SQ_G8) { r_from = SQ_H8; r_to = SQ_F8; }
            else if (sf_to == SQ_C8) { r_from = SQ_A8; r_to = SQ_D8; }

            if (r_from != SQ_NONE) {
                const Piece rook = global_pos.piece_on(r_from);
                global_pos.remove_piece(r_from);
                global_pos.put_piece(rook, r_to);
                dp.piece[dp.dirty_num] = rook;
                dp.from[dp.dirty_num] = r_from;
                dp.to[dp.dirty_num] = r_to;
                dp.dirty_num++;
            }
        }
        global_pos.put_piece(sf_piece, sf_to);
        dp.piece[dp.dirty_num] = sf_piece;
        dp.from[dp.dirty_num] = sf_from;
        dp.to[dp.dirty_num] = sf_to;
        dp.dirty_num++;
    }

    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = new_si;

    if (type_of(sf_piece) == PAWN || captured != NO_PIECE) {
        new_si->rule50 = 0;
    } else {
        new_si->rule50 = new_si->previous->rule50 + 1;
    }
}

void do_null_move(StateInfo *new_si) {
    new_si->previous = global_pos.state();
    new_si->accumulatorBig.computed[WHITE] = false;
    new_si->accumulatorBig.computed[BLACK] = false;
    new_si->accumulatorSmall.computed[WHITE] = false;
    new_si->accumulatorSmall.computed[BLACK] = false;
    new_si->nonPawnMaterial[WHITE] = new_si->previous->nonPawnMaterial[WHITE];
    new_si->nonPawnMaterial[BLACK] = new_si->previous->nonPawnMaterial[BLACK];
    new_si->dirtyPiece.dirty_num = 0;
    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = new_si;
    new_si->rule50 = new_si->previous->rule50 + 1;
}

void undo_move(int) {
    StateInfo *old_si = global_pos.state();
    StateInfo *prev_si = old_si->previous;
    const DirtyPiece &dp = old_si->dirtyPiece;

    for (int i = dp.dirty_num - 1; i >= 0; --i) {
        const Piece pc = dp.piece[i];
        const Square f = dp.from[i];
        const Square t = dp.to[i];

        if (t == SQ_NONE) {
            global_pos.put_piece(pc, f);
        } else {
            global_pos.remove_piece(t);
            if (f != SQ_NONE) {
                global_pos.put_piece(pc, f);
            }
        }
    }

    global_pos.sideToMove = ~global_pos.sideToMove;
    global_pos.st = prev_si;
}

int evaluate() {
    return Eval::evaluate(global_pos);
}

void hint_common_parent() {
    Eval::NNUE::hint_common_parent_position(global_pos);
}

void setup_reset(const std::string &fen) {
    setup_states.clear();
    setup_states.emplace_back();
    setup_states.back().previous = nullptr;
    global_pos.set(fen, &setup_states.back());
    stack_ptr = 0;
}

void setup_move(int move) {
    setup_states.emplace_back();
    StateInfo *next = &setup_states.back();
    do_move(move, next);
}

void setup_undo(int move) {
    undo_move(move);
    setup_states.pop_back();
}

const Position& get_global_pos() {
    return global_pos;
}

const std::deque<StateInfo>& get_setup_states() {
    return setup_states;
}

void sync_from_main_thread(const Position& main_pos, const std::deque<StateInfo>& main_setup) {
    setup_states = main_setup;
    std::memcpy(&global_pos, &main_pos, sizeof(Position));
    if (!setup_states.empty()) {
        global_pos.st = &setup_states.back();
        for (size_t i = 0; i < setup_states.size(); ++i) {
            setup_states[i].previous = (i == 0) ? nullptr : &setup_states[i - 1];
        }
    } else {
        global_pos.st = nullptr;
    }
    search_stack.resize(2048);
    stack_ptr = 0;
}

} // namespace Incremental
} // namespace Stockfish
