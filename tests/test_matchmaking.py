import ssl
import unittest
from unittest.mock import patch
from urllib.error import URLError

from tools.matchmaking import (
    LichessAPI,
    eligible_opponents,
    game_category,
    active_game_against,
)


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
            user("far-stronger", 2000), user("too-strong", 2501),
            user("inactive", 1500, games=0),
            user("AAthena101", 1500),
        ]

    def test_only_opponents_within_plus_minus_150_are_eligible(self):
        names = {u["username"] for u, _ in eligible_opponents(
            self.bots, "AAthena101", "blitz", 1500, now=100000
        )}
        self.assertEqual(names, {"lower-edge", "upper-edge"})
        self.assertNotIn("far-stronger", names)
        self.assertNotIn("too-strong", names)

    def test_recent_targets_are_skipped(self):
        names = {u["username"] for u, _ in eligible_opponents(
            self.bots, "AAthena101", "blitz", 1500,
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

    def test_outgoing_challenges_are_always_rated(self):
        client = LichessAPI("test-token", "AAthena101")
        with patch.object(client, "request", return_value=(200, {"id": "challenge-id"})) as request:
            status, result = client.challenge("opponent-bot", 180, 0)
        self.assertEqual(status, 200)
        self.assertEqual(result["id"], "challenge-id")
        request.assert_called_once_with("POST", "/api/challenge/opponent-bot", {
            "rated": "true",
            "variant": "standard",
            "clock.limit": "180",
            "clock.increment": "0",
        })

    def test_ssl_context_verifies_certificates_and_hostnames(self):
        client = LichessAPI("test-token", "AAthena101")
        self.assertEqual(client.ssl_context.verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(client.ssl_context.check_hostname)
        self.assertTrue(client.ssl_context.get_ca_certs())

    def test_certificate_error_has_actionable_message(self):
        client = LichessAPI("test-token", "AAthena101")
        reason = ssl.SSLCertVerificationError(1, "CERTIFICATE_VERIFY_FAILED")
        with patch("tools.matchmaking.urlopen", side_effect=URLError(reason)):
            with self.assertRaisesRegex(RuntimeError, "TLS certificate verification failed"):
                client.request("GET", "/api/account")


if __name__ == "__main__":
    unittest.main()
