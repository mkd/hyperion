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
 * @file main.cpp
 * @brief Program entry point: engine initialisation and CLI dispatch.
 *
 * Usage: `hyperion` starts the UCI loop; `hyperion bench [depth]` runs the
 * built-in benchmark and exits.
 */

#include "bitboard.h"
#include "crash_recovery.h"
#include "eval.h"
#include "position.h"
#include "search.h"
#include "tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    Threads.set(1);
    initBitboards();
    initRandomKeys();
    resetBoard();
    initEval(argv[0]);
    initLMR();
    TT::init(DefaultHashSize);
    setPosition(FenPosStartpos);

    if (argc > 1 && std::string(argv[1]) == "bench") {
        const int depth = (argc > 2) ? std::atoi(argv[2]) : 13;
        bench(depth);
        Threads.clear();
        tb_free();
        return 0;
    }

    uci();

    Threads.clear();
    tb_free();

    return 0;
}
