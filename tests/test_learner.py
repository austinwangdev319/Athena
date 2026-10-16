#!/usr/bin/env python3
import unittest

from tools.learn_from_games import expected_score, tune


class LearnerTests(unittest.TestCase):
    def test_expected_score_decreases_against_stronger_opponent(self):
        self.assertGreater(expected_score(500), 0.5)
        self.assertLess(expected_score(-500), 0.5)

    def test_expected_loss_against_much_stronger_bot_does_not_force_passivity(self):
        games = [(1, {
            "White": "AAthena101", "Black": "strongbot",
            "WhiteElo": "1500", "BlackElo": "2000", "Result": "0-1",
        }) for _ in range(5)]
        aggression, gambit_risk, note = tune(games, "AAthena101")
        self.assertGreaterEqual(aggression, 150)
        self.assertGreaterEqual(gambit_risk, 85)
        self.assertIn("expectation", note)

    def test_above_expectation_increases_initiative(self):
        games = [(1, {
            "White": "AAthena101", "Black": "equalbot",
            "WhiteElo": "1500", "BlackElo": "1500", "Result": "1-0",
        }) for _ in range(5)]
        aggression, gambit_risk, note = tune(games, "AAthena101")
        self.assertEqual((aggression, gambit_risk), (175, 110))
        self.assertIn("increasing initiative", note)

    def test_black_score_is_flipped_to_bot_perspective(self):
        games = [(1, {
            "White": "strongbot", "Black": "AAthena101",
            "WhiteElo": "2000", "BlackElo": "1500", "Result": "0-1",
        }) for _ in range(5)]
        aggression, gambit_risk, _ = tune(games, "AAthena101")
        self.assertEqual((aggression, gambit_risk), (175, 110))


if __name__ == "__main__":
    unittest.main()
