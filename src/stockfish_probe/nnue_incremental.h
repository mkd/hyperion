#ifndef NNUE_INCREMENTAL_H
#define NNUE_INCREMENTAL_H

#include <string>

namespace Stockfish {
namespace Incremental {

void init();
void push_state(int g_move);
void push_null_state();
void pop_state(int g_move);
void setup_reset(const std::string &fen);
void setup_move(int g_move);
void setup_undo(int g_move);
int evaluate();
void hint_common_parent();
const void* get_global_pos();
const void* get_setup_states();
void sync_from_main_thread(const void* main_pos, const void* main_setup);

} // namespace Incremental
} // namespace Stockfish

#endif // NNUE_INCREMENTAL_H
