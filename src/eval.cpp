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
 * @file eval.cpp
 * @brief NNUE network loading and evaluation entry point.
 */

#include "eval.h"

#include "nnue.h"
#include "stockfish_probe/probe.h"

#include <filesystem>
#include <string>

void initEval(const char* argv0) {
    std::string big_net   = "nn-b1a57edbea57.nnue";
    std::string small_net = "nn-baff1ede1f90.nnue";

    // If the nets are not in the working directory, look next to the executable
    // (and in its `src/` sibling) so Hyperion can be launched from anywhere.
    namespace fs = std::filesystem;
    if (!fs::exists(big_net) && argv0) {
        const fs::path exe_dir = fs::path(argv0).parent_path();
        if (fs::exists(exe_dir / big_net)) {
            big_net   = (exe_dir / big_net).string();
            small_net = (exe_dir / small_net).string();
        } else if (fs::exists(exe_dir / "src" / big_net)) {
            big_net   = (exe_dir / "src" / big_net).string();
            small_net = (exe_dir / "src" / small_net).string();
        }
    }

    Stockfish::Probe::init(big_net.c_str(), small_net.c_str());
}

int evaluate() noexcept {
    return Nnue::evaluate();
}
