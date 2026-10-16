#!/usr/bin/env python3
import unittest

from tools.learn_from_games import expected_score, tune


class LearnerTests(unittest.TestCase):
    def test_expected_score_accounts_for_rating_difference(self):
        self.assertLess(expected_score(500), 0.5)
        self.assertGreater(expected_score(-500), 0.5)

    def test_no_games_uses_conservative_defaults(self):
        aggression, gambit_risk, note = tune([], "AAthena101")
        self.assertEqual((aggression, gambit_risk), (80, 30))
        self.assertIn("conservative", note)

    def test_loss_to_much_stronger_bot_never_increases_risk(self):
        games = [(1, {
            "White": "AAthena101", "Black": "strongbot",
            "WhiteElo": "1500", "BlackElo": "2000", "Result": "0-1",
        }) for _ in range(5)]
        aggression, gambit_risk, note = tune(games, "AAthena101")
        self.assertLessEqual(aggression, 80)
        self.assertLessEqual(gambit_risk, 30)
        self.assertIn("rating expectation", note)

    def test_wins_do_not_automatically_increase_aggression(self):
        games = [(1, {
            "White": "AAthena101", "Black": "equalbot",
            "WhiteElo": "1500", "BlackElo": "1500", "Result": "1-0",
        }) for _ in range(5)]
        self.assertEqual(tune(games, "AAthena101")[:2], (80, 30))

    def test_black_score_is_flipped_to_bot_perspective(self):
        games = [(1, {
            "White": "strongbot", "Black": "AAthena101",
            "WhiteElo": "2000", "BlackElo": "1500", "Result": "0-1",
        }) for _ in range(5)]
        aggression, gambit_risk, _ = tune(games, "AAthena101")
        self.assertEqual((aggression, gambit_risk), (80, 30))


if __name__ == "__main__":
    unittest.main()
