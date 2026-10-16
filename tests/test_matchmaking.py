import unittest

from tools.matchmaking import eligible_opponents, game_category, active_game_against


def user(name, rating, games=20):
    return {"username": name, "perfs": {
        "blitz": {"rating": rating, "games": games},
        "rapid": {"rating": rating, "games": games},
    }}


class MatchmakingTests(unittest.TestCase):
    def setUp(self):
        self.bots = [
            user("lower-edge", 1350), user("upper-edge", 1650),
            user("too-low", 1349), user("too-high", 1651),
            user("training-edge", 2000), user("training-top-edge", 2500),
            user("too-strong", 2501), user("inactive", 1500, games=0),
            user("AAthena101", 1500),
        ]

    def test_rated_band_is_plus_minus_150_inclusive(self):
        names = {u["username"] for u, _ in eligible_opponents(
            self.bots, "AAthena101", "blitz", 1500, "rated", now=100000
        )}
        self.assertEqual(names, {"lower-edge", "upper-edge"})

    def test_training_band_is_500_to_1000_above(self):
        names = {u["username"] for u, _ in eligible_opponents(
            self.bots, "AAthena101", "blitz", 1500, "training", now=100000
        )}
        self.assertEqual(names, {"training-edge", "training-top-edge"})

    def test_recent_targets_are_skipped(self):
        names = {u["username"] for u, _ in eligible_opponents(
            self.bots, "AAthena101", "blitz", 1500, "rated",
            {"lower-edge": 99950}, now=100000
        )}
        self.assertEqual(names, {"upper-edge"})

    def test_clock_category_matches_lichess_conventions(self):
        self.assertEqual(game_category(180, 0), "blitz")
        self.assertEqual(game_category(300, 2), "blitz")
        self.assertEqual(game_category(600, 0), "rapid")
        self.assertEqual(game_category(600, 2), "rapid")

    def test_active_game_identifies_opponent_case_insensitively(self):
        games = [{"opponent": {"username": "BoTPlayer"}}]
        self.assertTrue(active_game_against(games, "botplayer"))
        self.assertFalse(active_game_against(games, "other"))


if __name__ == "__main__":
    unittest.main()
