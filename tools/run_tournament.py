#!/usr/bin/env python3
"""
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
"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import sys
import threading
import time

import chess
import chess.engine
import chess.pgn

# 25 Diverse, balanced openings for fair paired testing (each played as White & Black)
OPENINGS = [
    ("Standard Startpos", ""),
    ("Ruy Lopez (Morphy)", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6"),
    ("Italian Game (Giuoco)", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5"),
    ("Scotch Game", "e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4"),
    ("Sicilian Open (Classical)", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3"),
    ("Sicilian Najdorf", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6"),
    ("Sicilian Dragon", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6"),
    ("French Winawer", "e2e4 e7e6 d2d4 d7d5 b1c3 f8b4"),
    ("French Classical", "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6"),
    ("Caro-Kann Classical", "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5"),
    ("Caro-Kann Advance", "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5"),
    ("Queen's Gambit Declined", "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6"),
    ("Queen's Gambit Accepted", "d2d4 d7d5 c2c4 d5c4 g1f3 g8f6"),
    ("Slav Defense", "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6"),
    ("King's Indian Classical", "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8"),
    ("Nimzo-Indian Defense", "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4"),
    ("Queen's Indian Defense", "d2d4 g8f6 c2c4 e7e6 g1f3 b7b6"),
    ("Grunfeld Defense", "d2d4 g8f6 c2c4 g7g6 b1c3 d7d5"),
    ("Catalan Open", "d2d4 g8f6 c2c4 e7e6 g2g3 d7d5 f1g2 d5c4"),
    ("English Four Knights", "c2c4 e7e5 b1c3 g8f6 g1f3 b8c6"),
    ("Reti Opening", "g1f3 d7d5 c2c4 c7c6 g2g3 g8f6 f1g2"),
    ("Modern Defense", "e2e4 g7g6 d2d4 f8g7 b1c3 d7d6"),
    ("Pirc Defense", "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6"),
    ("Alekhine Defense", "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6"),
    ("Dutch Defense", "d2d4 f7f5 c2c4 g8f6 g2g3 e7e6 f1g2")
]


def play_single_game(game_id, new_path, base_path, time_limit=0.06, max_moves=120):
    """Plays one game between Hyperion-New and Hyperion-Base."""
    opening_idx = ((game_id - 1) // 2) % len(OPENINGS)
    is_reverse = ((game_id - 1) % 2) == 1
    op_name, op_moves = OPENINGS[opening_idx]

    white_is_new = not is_reverse
    white_name = "Hyperion-New" if white_is_new else "Hyperion-Base"
    black_name = "Hyperion-Base" if white_is_new else "Hyperion-New"
    white_path = new_path if white_is_new else base_path
    black_path = base_path if white_is_new else new_path

    game_record = chess.pgn.Game()
    game_record.headers["Event"] = "Hyperion Evaluation Match"
    game_record.headers["Site"] = "Localhost"
    game_record.headers["Date"] = time.strftime("%Y.%m.%d")
    game_record.headers["Round"] = str(game_id)
    game_record.headers["White"] = white_name
    game_record.headers["Black"] = black_name
    game_record.headers["Opening"] = op_name

    board = game_record.board()
    node = game_record
    if op_moves:
        for m in op_moves.split():
            move_obj = chess.Move.from_uci(m)
            board.push(move_obj)
            node = node.add_variation(move_obj)

    white_eng = chess.engine.SimpleEngine.popen_uci(white_path)
    black_eng = chess.engine.SimpleEngine.popen_uci(black_path)
    white_eng.configure({"Threads": 1, "Hash": 16})
    black_eng.configure({"Threads": 1, "Hash": 16})

    limit = chess.engine.Limit(time=time_limit)

    result = "*"
    reason = "undecided"
    consecutive_high_eval = 0
    leading_side = None

    try:
        while not board.is_game_over() and board.fullmove_number <= max_moves:
            eng = white_eng if board.turn == chess.WHITE else black_eng
            play_res = eng.play(board, limit, info=chess.engine.INFO_SCORE)
            move = play_res.move
            if move is None:
                break

            board.push(move)
            node = node.add_variation(move)

            # Adjudication checks
            if board.is_checkmate():
                result = "1-0" if board.turn == chess.BLACK else "0-1"
                reason = "checkmate"
                break
            if board.is_stalemate():
                result = "1/2-1/2"
                reason = "stalemate"
                break
            if board.is_insufficient_material():
                result = "1/2-1/2"
                reason = "insufficient material"
                break
            if board.can_claim_threefold_repetition():
                result = "1/2-1/2"
                reason = "3-fold repetition"
                break
            if board.can_claim_fifty_moves():
                result = "1/2-1/2"
                reason = "50-move rule"
                break

            # Eval adjudication: if evaluation is >= +8.00 cp for 4 plies after move 20
            score_obj = play_res.info.get("score") if play_res.info else None
            if score_obj and board.fullmove_number >= 20:
                last_player = not board.turn
                cp_pov = score_obj.pov(last_player).score(mate_score=30000)
                if cp_pov is not None and cp_pov >= 800:
                    if leading_side == last_player:
                        consecutive_high_eval += 1
                        if consecutive_high_eval >= 4:
                            result = "1-0" if last_player == chess.WHITE else "0-1"
                            reason = "adjudication (+8.00 eval)"
                            break
                    else:
                        leading_side = last_player
                        consecutive_high_eval = 1
                else:
                    consecutive_high_eval = 0

        if result == "*":
            if board.fullmove_number > max_moves:
                result = "1/2-1/2"
                reason = "max moves reached"
            elif board.is_game_over():
                result = board.result()
                reason = "rules"
            else:
                result = "1/2-1/2"
                reason = "draw"

    finally:
        white_eng.quit()
        black_eng.quit()

    game_record.headers["Result"] = result
    game_record.headers["Termination"] = reason

    # Determine winner relative to Hyperion-New
    if result == "1-0":
        winner = "Hyperion-New" if white_is_new else "Hyperion-Base"
        new_point = 1.0 if white_is_new else 0.0
    elif result == "0-1":
        winner = "Hyperion-Base" if white_is_new else "Hyperion-New"
        new_point = 0.0 if white_is_new else 1.0
    else:
        winner = "Draw"
        new_point = 0.5

    return {
        "game_id": game_id,
        "white": white_name,
        "black": black_name,
        "opening": op_name,
        "result": result,
        "winner": winner,
        "new_point": new_point,
        "moves": board.fullmove_number,
        "reason": reason,
        "pgn": str(game_record)
    }


def main():
    parser = argparse.ArgumentParser(description="Hyperion Engine Tournament Runner")
    parser.add_argument("--new", default="./hyperion", help="Path to new Hyperion binary")
    parser.add_argument("--base", default="/tmp/hyperion_baseline_build/hyperion", help="Path to baseline Hyperion binary")
    parser.add_argument("--start", type=int, default=1, help="Start game number (1-based)")
    parser.add_argument("--end", type=int, default=5, help="End game number (1-based)")
    parser.add_argument("--total", type=int, default=50, help="Total games in match")
    parser.add_argument("--time", type=float, default=0.06, help="Time limit per move (seconds)")
    parser.add_argument("--workers", type=int, default=4, help="Parallel game workers")
    parser.add_argument("--state", default="tournament_state.json", help="Tournament state JSON file")
    parser.add_argument("--pgn", default="tournament_games.pgn", help="Tournament PGN output file")

    args = parser.parse_args()

    # Load existing state if available
    state = {"new_wins": 0, "base_wins": 0, "draws": 0, "games": []}
    if os.path.exists(args.state):
        try:
            with open(args.state, "r") as f:
                state = json.load(f)
        except Exception:
            pass

    game_ids = list(range(args.start, args.end + 1))
    results = []

    # Run games in parallel pool and stream updates immediately
    lock = threading.Lock()
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        future_to_id = {
            executor.submit(play_single_game, gid, args.new, args.base, args.time): gid
            for gid in game_ids
        }
        for future in as_completed(future_to_id):
            r = future.result()
            with lock:
                if r["new_point"] == 1.0:
                    state["new_wins"] += 1
                elif r["new_point"] == 0.0:
                    state["base_wins"] += 1
                else:
                    state["draws"] += 1

                state["games"].append({
                    "game_id": r["game_id"],
                    "white": r["white"],
                    "black": r["black"],
                    "opening": r["opening"],
                    "result": r["result"],
                    "winner": r["winner"],
                    "moves": r["moves"],
                    "reason": r["reason"]
                })

                with open(args.pgn, "a") as pgn_file:
                    pgn_file.write(r["pgn"] + "\n\n")

                with open(args.state, "w") as f:
                    json.dump(state, f, indent=2)

                total_played = state["new_wins"] + state["base_wins"] + state["draws"]
                points = state["new_wins"] + 0.5 * state["draws"]
                pct = (points / total_played) * 100.0 if total_played > 0 else 0.0

                print(f"Game {r['game_id']:03d}/{args.total:03d}: {r['white']:13s} vs {r['black']:13s} | "
                      f"{r['opening']:23s} | {r['result']:7s} ({r['moves']:2d} moves, {r['reason']:15s}) | "
                      f"Score: +{state['new_wins']} -{state['base_wins']} ={state['draws']} ({pct:.1f}%)",
                      flush=True)

    # Print final summary if all requested games have completed
    total = state["new_wins"] + state["base_wins"] + state["draws"]
    if total > 0:
        w = state["new_wins"]
        l = state["base_wins"]
        d = state["draws"]
        score = (w + 0.5 * d) / total
        import math
        if score <= 0.0:
            elo = -999.0
            error = 0.0
        elif score >= 1.0:
            elo = 999.0
            error = 0.0
        else:
            elo = -400.0 * math.log10(1.0 / score - 1.0)
            var = (w * (1.0 - score)**2 + l * (0.0 - score)**2 + d * (0.5 - score)**2) / (total - 1 if total > 1 else 1)
            se = math.sqrt(var / total)
            error = 400.0 / (math.log(10) * score * (1.0 - score)) * 1.96 * se

        print("\n" + "=" * 60, flush=True)
        print(f"  TOURNAMENT SUMMARY: Hyperion-New vs Hyperion-Base", flush=True)
        print("=" * 60, flush=True)
        print(f"  Total Games : {total}", flush=True)
        print(f"  Score       : +{w} -{l} ={d}  ({score * 100:.1f}%)", flush=True)
        print(f"  Elo Diff    : {elo:+.1f} +/- {error:.1f} (95% CI)", flush=True)
        print("=" * 60 + "\n", flush=True)


if __name__ == "__main__":
    main()
