/*
  Hyperion - A Modern C++26 UCI Chess Engine
  Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>

  Hyperion is a successor of Gargantua, which is derived from Chess0 and
  inspired by Code Monkey King's bbc-1.4.

  This file is part of Hyperion.  It builds on the Stockfish NNUE code in this
  directory (Copyright (C) the Stockfish developers, GPL-3.0-or-later).

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
 * @file probe.cpp
 * @brief Implementation of the stateless NNUE evaluation entry points.
 */

#include "probe.h"
#include "bitboard.h"
#include "position.h"
#include "evaluate.h"
#include "nnue/nnue_architecture.h"

namespace Stockfish {
namespace Probe {

/// Initialises the bitboard tables and loads both NNUE networks.
void init(const char *bigNetFile, const char *smallNetFile) {
    Bitboards::init();

    std::unordered_map<Eval::NNUE::NetSize, Eval::EvalFile> evalFiles = {
        {Eval::NNUE::Big,   {"EvalFile",      bigNetFile,   "None", ""}},
        {Eval::NNUE::Small, {"EvalFileSmall", smallNetFile, "None", ""}}
    };

    evalFiles = Eval::NNUE::load_networks("", evalFiles);
}

/// One-shot evaluation of a FEN position.
int eval(const char *fen) {
    Position pos;
    StateListPtr states(new std::deque<StateInfo>(1));
    pos.set(fen, &states->back());
    return Eval::evaluate(pos);
}

/// One-shot evaluation of a piece-board position.
int eval(const int pieceBoard[], bool side, int rule50) {
    Position pos;
    StateListPtr states(new std::deque<StateInfo>(1));
    pos.set(pieceBoard, side, rule50, &states->back());
    return Eval::evaluate(pos);
}

/// One-shot evaluation of a piece/square-list position.
int eval(const int pieces[], const int squares[], int pieceAmount, bool side, int rule50) {
    Position pos;
    StateListPtr states(new std::deque<StateInfo>(1));
    pos.set(pieces, squares, pieceAmount, side, rule50, &states->back());
    return Eval::evaluate(pos);
}

} // namespace Probe
} // namespace Stockfish
