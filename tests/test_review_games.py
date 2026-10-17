import unittest

import chess
import chess.pgn

from tools.review_games import (
    BASE_AGGRESSION,
    BASE_GAMBIT_RISK,
    activity_is_active,
    candidate_moves,
    derive_style,
    move_regret,
    score_from_uci_info,
)


class ReviewGamesTests(unittest.TestCase):
    def test_move_regret_compares_pre_and_post_move_scores(self):
        self.assertEqual(move_regret(220, 180), 376)
        self.assertEqual(move_regret(100, -120), 0)
        self.assertIsNone(move_regret(None, 10))

    def test_uci_score_parser_handles_centipawns_and_mate(self):
        self.assertEqual(score_from_uci_info("info depth 8 score cp 145 nodes 12000"), 145)
        self.assertEqual(score_from_uci_info("info depth 9 score mate -2 nodes 12000"), -29800)
        self.assertIsNone(score_from_uci_info("info depth 8 score cp 200 lowerbound nodes 12000"))

    def test_style_defaults_are_conservative(self):
        aggression, gambit, stats = derive_style([])
        self.assertEqual((aggression, gambit), (BASE_AGGRESSION, BASE_GAMBIT_RISK))
        self.assertLessEqual(aggression, 80)
        self.assertLessEqual(gambit, 30)
        self.assertEqual(stats["reviewed_moves"], 0)

    def test_repeat_blunders_only_reduce_risk_settings(self):
        clean = {"moves": [{"regret_cp": 20, "pawn_move": True}] * 12}
        aggression0, gambit0, _ = derive_style([clean])
        noisy = {"moves": [
            {"regret_cp": 520, "pawn_move": True},
            {"regret_cp": 460, "pawn_move": True},
            {"regret_cp": 300, "pawn_move": False},
            {"regret_cp": 230, "pawn_move": True},
        ]}
        aggression1, gambit1, stats = derive_style([clean, noisy])
        self.assertLessEqual(aggression1, aggression0)
        self.assertLess(gambit1, gambit0)
        self.assertEqual(stats["blunders"], 2)
        self.assertGreaterEqual(aggression1, 55)
        self.assertGreaterEqual(gambit1, 5)

    def test_candidate_selection_only_reviews_athena_moves(self):
        pgn = """[Event "Test"]
[Site "local"]
[Date "2026.10.16"]
[White "Athena"]
[Black "Opponent"]
[Result "1-0"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 1-0
"""
        game = chess.pgn.read_game(__import__("io").StringIO(pgn))
        moves = candidate_moves(game, "athena", max_positions=10)
        self.assertTrue(moves)
        self.assertTrue(all(int(item["ply"]) % 2 == 1 for item in moves))
        self.assertLessEqual(len(moves), 10)

    def test_activity_flag_is_read_safely(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "match_state.json"
            self.assertFalse(activity_is_active(path))
            path.write_text('{"active_game": true}', encoding="utf-8")
            self.assertTrue(activity_is_active(path))


if __name__ == "__main__":
    unittest.main()
