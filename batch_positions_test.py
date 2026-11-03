#!/usr/bin/env python3
"""Check the small embedded-position runner without searching real engines.

Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later. See LICENSE; no warranty.
"""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import batch_positions as batch
import chess
import chess.engine

FEN = "7k/8/8/8/8/8/8/K7 w - - 0 1"
MOVE = chess.Move.from_uci("a1a2")


class BatchTests(unittest.TestCase):
    def engine_fixture(self, infos, move=MOVE):
        engine = mock.MagicMock()
        engine.__enter__.return_value = engine
        search = engine.analysis.return_value.__enter__.return_value
        search.__iter__.return_value = iter(infos)
        search.wait.return_value = SimpleNamespace(move=move)
        return engine

    def test_embedded_positions_are_valid(self):
        self.assertEqual(len(batch.FENS), 39)
        for index, fen in enumerate(batch.FENS, 1):
            with self.subTest(position=index):
                self.assertTrue(chess.Board(fen).is_valid())

    def test_format_mates_and_centipawns(self):
        for score, result in [(None, "NO SCORE"), (chess.engine.Mate(7), "M7"),
                              (chess.engine.Mate(-8), "-M8"), (chess.engine.Mate(0), "M0"),
                              (chess.engine.Cp(123), "+1.23"), (chess.engine.Cp(-25), "-0.25")]:
            with self.subTest(score=score):
                self.assertEqual(batch.format_score(score), result)

    def test_latest_exact_score_and_settings(self):
        score = lambda n: chess.engine.PovScore(chess.engine.Mate(n), chess.WHITE)
        engine = self.engine_fixture([
            {"score": score(9)}, {"score": score(7), "pv": [MOVE]},
            {"score": score(6), "lowerbound": True}, {"score": score(5), "upperbound": True},
            {"score": score(4), "multipv": 2}, {"string": "diagnostics score mate 3"}])
        with mock.patch.object(chess.engine.SimpleEngine, "popen_uci", return_value=engine), \
                mock.patch.object(batch, "Timer") as timer:
            self.assertEqual(batch.analyze(Path("/fake/engine"), FEN), ("M7", "a1a2"))
        engine.configure.assert_called_once_with({"Threads": batch.THREADS, "Hash": batch.HASH_MB})
        self.assertEqual(engine.analysis.call_args.args[1].time, batch.MOVETIME_MS / 1000)
        engine.quit.assert_called_once()
        timer.return_value.start.assert_called_once()
        timer.return_value.cancel.assert_called_once()

    def test_score_is_from_side_to_move_and_missing_score_is_explicit(self):
        move = chess.Move.from_uci("h8h7")
        for infos, expected in [([], "NO SCORE"),
                                ([{"score": chess.engine.PovScore(chess.engine.Mate(8), chess.WHITE)}], "-M8")]:
            with self.subTest(expected=expected), \
                    mock.patch.object(chess.engine.SimpleEngine, "popen_uci",
                                      return_value=self.engine_fixture(infos, move)), \
                    mock.patch.object(batch, "Timer"):
                self.assertEqual(batch.analyze(Path("/fake/engine"), FEN.replace(" w ", " b ")),
                                 (expected, "h8h7"))

    def test_engine_errors_and_mismatched_pv_cleanup(self):
        cases = [([{"string": "error: missing network"}], "missing network"),
                 ([{"score": chess.engine.PovScore(chess.engine.Cp(10), chess.WHITE),
                    "pv": [chess.Move.from_uci("a1b1")]}], "final best move")]
        for infos, message in cases:
            engine = self.engine_fixture(infos)
            with self.subTest(message=message), \
                    mock.patch.object(chess.engine.SimpleEngine, "popen_uci", return_value=engine), \
                    mock.patch.object(batch, "Timer") as timer:
                with self.assertRaisesRegex(RuntimeError, message):
                    batch.analyze(Path("/fake/engine"), FEN)
                timer.return_value.cancel.assert_called_once()
                engine.__exit__.assert_called_once()

    def test_main_keeps_order_and_continues_after_failure(self):
        stdout = io.StringIO()
        with mock.patch.object(sys, "argv", ["batch_positions.py", __file__]), \
                mock.patch.object(batch, "FENS", ["first", "second", "third", "fourth"]), \
                mock.patch.object(batch, "analyze", side_effect=[("M7", "a1a2"),
                    RuntimeError("fixture failed"), ("NO SCORE", "a1a2"), ("M8", "a1a2")]) as analyze, \
                contextlib.redirect_stdout(stdout):
            self.assertEqual(batch.main(), 1)
        self.assertEqual([call.args[1] for call in analyze.call_args_list],
                         ["first", "second", "third", "fourth"])
        self.assertIn("ERROR: fixture failed", stdout.getvalue())
        self.assertIn("04/4  M8", stdout.getvalue())

    def test_each_position_uses_a_fresh_real_subprocess(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            engine, journal = root / "synthetic engine", root / "starts.txt"
            engine.write_text("import os, sys\n"
                f"with open({str(journal)!r}, 'a') as f: f.write(str(os.getpid()) + '\\n')\n"
                "for command in sys.stdin:\n"
                " if command.strip() == 'uci':\n"
                "  print('option name Threads type spin default 1 min 1 max 8')\n"
                "  print('option name Hash type spin default 16 min 1 max 512')\n"
                "  print('uciok', flush=True)\n"
                " elif command.strip() == 'isready': print('readyok', flush=True)\n"
                " elif command.startswith('go '):\n"
                "  print('info depth 2 score mate 7 pv a1a2')\n"
                "  print('bestmove a1a2', flush=True)\n"
                " elif command.strip() == 'quit': break\n", encoding="utf-8")
            # Explicit interpreter arguments work on Windows and with spaces
            # in the managed environment path; shebangs cannot represent that.
            popen = chess.engine.SimpleEngine.popen_uci
            with mock.patch.object(batch, "MOVETIME_MS", 1), \
                    mock.patch.object(chess.engine.SimpleEngine, "popen_uci",
                        side_effect=lambda path, **kwargs: popen(
                            [sys.executable, path], **kwargs)):
                self.assertEqual(batch.analyze(engine, FEN), ("M7", "a1a2"))
                self.assertEqual(batch.analyze(engine, FEN), ("M7", "a1a2"))
            starts = journal.read_text().splitlines()
            self.assertEqual(len(starts), 2)
            self.assertNotEqual(starts[0], starts[1])


if __name__ == "__main__":
    unittest.main()
