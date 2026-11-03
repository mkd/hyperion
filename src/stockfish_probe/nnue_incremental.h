#ifndef NNUE_INCREMENTAL_H
#define NNUE_INCREMENTAL_H

#include "position.h"
#include <deque>
#include <string>

namespace Stockfish {
namespace Incremental {

void init();
void do_move(int g_move, StateInfo *si);
void do_null_move(StateInfo *si);
void undo_move(int g_move);

void setup_reset(const std::string &fen);
void setup_move(int g_move);
void setup_undo(int g_move);

void push_state(int g_move);
void push_null_state();
void pop_state(int g_move);

int evaluate();
void hint_common_parent();

const Position& get_global_pos();
const std::deque<StateInfo>& get_setup_states();
void sync_from_main_thread(const Position& main_pos, const std::deque<StateInfo>& main_setup);

} // namespace Incremental
} // namespace Stockfish

#endif
