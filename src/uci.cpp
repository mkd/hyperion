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
 * @file uci.cpp
 * @brief UCI command loop, `go` time budgeting, perft and the bench suite.
 */

#include "uci.h"
#include "bitboard.h"
#include "eval.h"
#include "movgen.h"
#include "nnue.h"
#include "position.h"
#include "search.h"
#include "tbprobe.h"
#include "thread.h"
#include "tt.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

using std::cin, std::cout, std::flush, std::getline, std::istringstream, std::string, std::to_string, std::vector, std::ws;

uint64_t perft_nodes = 0;

/// Recursive perft: counts leaf nodes, verifying legality with makeMove().
void perft_driver(int depth) {
    if (depth == 0) {
        perft_nodes++;
        return;
    }

    MoveList_t moveList;
    moveList.count = 0;
    generateMoves(moveList);

    for (int move_count = 0; move_count < moveList.count; ++move_count) {
        const BoardSnapshot snapshot;
        if (!makeMove(moveList.moves[move_count])) {
            snapshot.restore();
            continue;
        }
        perft_driver(depth - 1);
        snapshot.restore();
    }
}

/// Root perft split ("divide"): prints the node count below each legal root move.
void perft_test(int depth) {
    cout << "\n     Performance test\n\n";

    MoveList_t moveList;
    moveList.count = 0;
    generateMoves(moveList);

    const uint64_t start = getTimeMs();

    for (int move_count = 0; move_count < moveList.count; ++move_count) {
        const BoardSnapshot snapshot;
        if (!makeMove(moveList.moves[move_count])) {
            snapshot.restore();
            continue;
        }

        const uint64_t cum_nodes = perft_nodes;
        perft_driver(depth - 1);
        const uint64_t old_nodes = perft_nodes - cum_nodes;

        snapshot.restore();

        cout << "     move: " << SquareToCoordinates[getMoveSource(moveList.moves[move_count])]
             << SquareToCoordinates[getMoveTarget(moveList.moves[move_count])];
        if (int pr = getPromo(moveList.moves[move_count]); pr != 0)
            cout << promoChar(pr);
        cout << "  nodes: " << old_nodes << "\n";
    }

    const uint64_t elapsed = std::max<uint64_t>(1, getTimeMs() - start);
    cout << "\n    Depth: " << depth;
    cout << "\n    Nodes: " << perft_nodes;
    cout << "\n     Time: " << elapsed << " ms";
    cout << "\n      NPS: " << (perft_nodes * 1000ULL) / elapsed << "\n\n";
}

/// Matches `move_str` against the legal moves of the current position (0 if none matches).
int parseMove(const string &move_str) {
    if (move_str.length() < 4)
        return 0;

    MoveList_t moveList;
    moveList.count = 0;
    generateMoves(moveList);

    const int fromSq = (move_str[0] - 'a') + (8 - (move_str[1] - '0')) * 8;
    const int toSq   = (move_str[2] - 'a') + (8 - (move_str[3] - '0')) * 8;

    for (int count = 0; count < moveList.count; ++count) {
        const int move = moveList.moves[count];
        if (getMoveSource(move) != fromSq || getMoveTarget(move) != toSq)
            continue;

        // A promotion move must carry the matching promotion letter.
        if (const int promo = getPromo(move); promo) {
            if (move_str.length() < 5 || promoChar(promo) != move_str[4])
                continue;
        }

        // Accept the move only if it is legal.
        const BoardSnapshot snapshot;
        const bool legal = makeMove(move);
        snapshot.restore();
        return legal ? move : 0;
    }
    return 0;
}

/// Sets up the root position from `position startpos|fen ... [moves ...]`, keeping NNUE and repetition history in sync.
void parsePosition(const string &command) {
    Threads.wait_for_search_finished();
    current_thread = &Threads.main()->state;

    istringstream ss(command);
    string token;
    ss >> token; // "position"
    ss >> token;

    if (token == "startpos") {
        setPosition(FenPosStartpos);
        ss >> token; // maybe "moves"
    } else if (token == "fen") {
        string fen;
        while (ss >> token && token != "moves") {
            if (!fen.empty()) fen += " ";
            fen += token;
        }
        if (fen.empty() || !setPosition(fen)) {
            setPosition(FenPosStartpos);
        }
    }

    if (token == "moves") {
        while (ss >> token) {
            const int move = parseMove(token);
            if (!move) break;
            Nnue::setupMove(move);
            makeMove(move);
            repetition_index++;
            repetition_table[repetition_index] = hash_key;
        }
    }

    root_repetition_index = repetition_index;
}

/**
 * Parses `go` arguments, derives the soft (optimum) and hard (maximum) time
 * limits from the clock, increment and moves-to-go, and starts the search.
 */
void parseGo(const string &line) {
    Threads.wait_for_search_finished();
    current_thread = &Threads.main()->state;

    Limits.starttime    = getTimeMs();
    Limits.opt_stoptime = 0;
    Limits.max_stoptime = 0;
    Limits.stoptime     = 0;
    Limits.depth        = -1;
    Limits.time         = -1;
    Limits.inc          = 0;
    Limits.movetime     = -1;
    Limits.movestogo    = 0;
    Limits.timeset      = false;
    Limits.stopped.store(false, std::memory_order_relaxed);
    Limits.quit         = 0;

    istringstream ss(line);
    string token;
    ss >> token; // "go"

    while (ss >> token) {
        if (token == "infinite") {
            Limits.timeset = false;
        } else if (token == "wtime" && sideToMove == White) {
            ss >> Limits.time;
        } else if (token == "btime" && sideToMove == Black) {
            ss >> Limits.time;
        } else if (token == "winc" && sideToMove == White) {
            ss >> Limits.inc;
        } else if (token == "binc" && sideToMove == Black) {
            ss >> Limits.inc;
        } else if (token == "movestogo") {
            ss >> Limits.movestogo;
        } else if (token == "movetime") {
            ss >> Limits.movetime;
        } else if (token == "depth") {
            ss >> Limits.depth;
        } else if (token == "perft") {
            int perft_d = 1;
            ss >> perft_d;
            perft_nodes = 0;
            perft_test(perft_d);
            return;
        }
    }

    if (Limits.movetime != -1) {
        Limits.timeset = true;
        const int mt = std::max(1, Limits.movetime - 15);
        Limits.opt_stoptime = Limits.starttime + mt;
        Limits.max_stoptime = Limits.starttime + mt;
        Limits.stoptime     = Limits.max_stoptime;
    } else if (Limits.time != -1) {
        Limits.timeset = true;
        const int move_overhead = 20;
        const int avail_time = std::max(1, Limits.time - move_overhead);

        int opt_ms = 0;
        int max_ms = 0;

        if (Limits.movestogo > 0) {
            const int mtg = std::min(Limits.movestogo, 50);
            opt_ms = static_cast<int>(avail_time * (0.85 / mtg) + Limits.inc * 0.75);
            max_ms = static_cast<int>(avail_time * std::min(0.80, 3.5 / mtg));
        } else {
            opt_ms = avail_time / 25 + (Limits.inc * 3) / 4;
            max_ms = std::min((avail_time * 3) / 4, opt_ms * 4);
        }

        opt_ms = std::clamp(opt_ms, 1, std::max(1, avail_time - 5));
        max_ms = std::clamp(max_ms, opt_ms, std::max(1, avail_time - 5));

        Limits.opt_stoptime = Limits.starttime + opt_ms;
        Limits.max_stoptime = Limits.starttime + max_ms;
        Limits.stoptime     = Limits.max_stoptime;
    }

    if (Limits.depth == -1) {
        Limits.depth = MaxSearchDepth - 1;
    }

    TT::new_search();
    search();
}

/// Fixed-depth search over 16 benchmark positions; the node count is a regression fingerprint.
void bench(int depth) {
    static const vector<string> bench_fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "r3r1k1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
        "r1bq1r1k/1pp1n1pb/p1n1p2p/3pP3/P2P1BNP/2PB1N2/1P2QPP1/R4RK1 w - - 2 14",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Np2p/2P1P3/3P1PB1/PP2B1PP/R2Q1RK1 b k - 0 11",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
        "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
        "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26"
    };

    Threads.wait_for_search_finished();
    TT::clear();
    Threads.clear_histories();

    uint64_t total_nodes = 0;
    const uint64_t start_time = getTimeMs();

    for (size_t i = 0; i < bench_fens.size(); ++i) {
        cout << "\nPosition [" << (i + 1) << "/" << bench_fens.size() << "]: " << bench_fens[i] << "\n";
        parsePosition("position fen " + bench_fens[i]);
        parseGo("go depth " + to_string(depth));
        Threads.wait_for_search_finished();
        total_nodes += Threads.nodes_searched();
    }

    const uint64_t elapsed = std::max<uint64_t>(1, getTimeMs() - start_time);
    const uint64_t nps = (total_nodes * 1000ULL) / elapsed;

    cout << "\n========================================\n";
    cout << "Total Time  : " << elapsed << " ms\n";
    cout << "Total Nodes : " << total_nodes << "\n";
    cout << "Nodes/sec   : " << nps << "\n";
    cout << "========================================\n" << flush;
}

/// UCI main loop (isready, position, go, stop, setoption, ... plus debug commands `d`, `eval`, `flip`, `bench`).
static string SyzygyPath = "<empty>";

static void initSyzygyAutoPath() {
    std::error_code ec;
    for (const char* candidate : {"../syzygy", "./syzygy", "syzygy"}) {
        if (std::filesystem::is_directory(candidate, ec)) {
            SyzygyPath = candidate;
            tb_init(SyzygyPath.c_str());
            break;
        }
    }
}

void uci() {
    initSyzygyAutoPath();
    string input;
    while (getline(cin, input)) {
        if (input.empty()) continue;

        istringstream ss(input);
        string token;
        ss >> token;

        if (token == "isready") {
            cout << "readyok\n" << flush;
        } else if (token == "position") {
            parsePosition(input);
        } else if (token == "ucinewgame") {
            Threads.wait_for_search_finished();
            TT::clear();
            Threads.clear_histories();
            parsePosition("position startpos");
        } else if (token == "go") {
            parseGo(input);
        } else if (token == "stop") {
            Limits.stopped.store(true, std::memory_order_relaxed);
            Threads.wait_for_search_finished();
        } else if (token == "quit") {
            if (!Limits.timeset && Limits.depth >= MaxSearchDepth - 1) {
                Limits.stopped.store(true, std::memory_order_relaxed);
            }
            Threads.wait_for_search_finished();
            break;
        } else if (token == "uci") {
            cout << "id name " << EngineName << " " << EngineVersion << "\n";
            cout << "id author " << EngineAuthor << "\n";
            cout << "option name Hash type spin default " << DefaultHashSize
                 << " min " << MinHashSize << " max " << MaxHashSize << "\n";
            cout << "option name Threads type spin default 1 min 1 max 1024\n";
            cout << "option name MultiPV type spin default " << DefaultMultiPV
                 << " min " << MinMultiPV << " max " << MaxMultiPV << "\n";
            cout << "option name SyzygyPath type string default " << SyzygyPath << "\n";
            cout << "uciok\n" << flush;
        } else if (token == "setoption") {
            Threads.wait_for_search_finished();
            string name_tok, opt_name;
            ss >> name_tok; // "name"
            ss >> opt_name;
            if (opt_name == "Hash") {
                string val_tok;
                uint32_t mb = DefaultHashSize;
                ss >> val_tok >> mb;
                mb = std::clamp(mb, MinHashSize, MaxHashSize);
                TT::init(mb);
            } else if (opt_name == "Threads") {
                string val_tok;
                size_t num_threads = 1;
                ss >> val_tok >> num_threads;
                num_threads = std::clamp<size_t>(num_threads, 1, 1024);
                Threads.set(num_threads);
            } else if (opt_name == "MultiPV") {
                string val_tok;
                int mpv = DefaultMultiPV;
                ss >> val_tok >> mpv;
                MultiPV = std::clamp(mpv, MinMultiPV, MaxMultiPV);
            } else if (opt_name == "SyzygyPath") {
                string val_tok, path;
                ss >> val_tok; // "value"
                getline(ss >> ws, path);
                if (!path.empty() && path != "<empty>") {
                    SyzygyPath = path;
                    tb_init(SyzygyPath.c_str());
                } else {
                    SyzygyPath = "<empty>";
                    tb_init("");
                }
            }
        } else if (token == "d") {
            Threads.wait_for_search_finished();
            printBoard();
        } else if (token == "eval") {
            Threads.wait_for_search_finished();
            const int raw = evaluate();
            const int adj = adjustEvalWithCorrHist(raw);
            cout << "Raw NNUE eval: " << raw << " cp | Adjusted eval: " << adj << " cp\n";
        } else if (token == "flip") {
            Threads.wait_for_search_finished();
            flip = !flip;
            sideToMove ^= 1;
            printBoard();
        } else if (token == "bench") {
            int d = 13;
            if (!(ss >> d)) d = 13;
            bench(d);
        }
    }
    Threads.wait_for_search_finished();
}
