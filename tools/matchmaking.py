#!/usr/bin/env python3
"""Rating-aware outgoing matchmaking for Athena.

Rated games are limited to opponents within +/-150 rating points. About one
out of every four accepted games is a casual training challenge against a bot
500-1000 points stronger, when one is available. This collects stronger-opponent
games for the existing results-based style tuner; it is not neural-network training.
"""
import argparse
import json
import os
import random
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path
import ssl
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

try:
    import certifi
except ImportError:
    certifi = None

API_BASE = "https://lichess.org"
CLOSE_RATING = 150
TRAINING_MIN_ADVANTAGE = 500
TRAINING_MAX_ADVANTAGE = 1000
MAX_ACCEPTED_GAMES_PER_UTC_DAY = 80  # Leave room below Lichess's daily bot-vs-bot cap for inbound games.
CHALLENGE_COOLDOWN_SECONDS = 65
RECENT_TARGET_SECONDS = 6 * 60 * 60
CONTROL_CHOICES = {
    "blitz": [(180, 0), (300, 2)],
    "rapid": [(600, 0), (600, 2)],
}


def game_category(base_time, increment):
    duration = base_time + increment * 40
    if duration < 179:
        return "bullet"
    if duration < 479:
        return "blitz"
    if duration < 1499:
        return "rapid"
    return "classical"


def rating_for(profile, category, require_games=False):
    perf = profile.get("perfs", {}).get(category, {})
    if not isinstance(perf, dict):
        return None
    rating = perf.get("rating")
    if not isinstance(rating, (int, float)):
        return None
    if require_games and int(perf.get("games", 0) or 0) < 1:
        return None
    return int(rating)


def eligible_opponents(users, username, category, own_rating, mode,
                       recent_targets=None, now=None):
    """Return established online bots in the requested rating band."""
    recent_targets = recent_targets or {}
    now = time.time() if now is None else now
    result = []
    for user in users:
        name = user.get("username", "")
        if not name or name.casefold() == username.casefold():
            continue
        last_attempt = recent_targets.get(name.casefold())
        if last_attempt is not None and now - float(last_attempt) < RECENT_TARGET_SECONDS:
            continue
        rating = rating_for(user, category, require_games=True)
        if rating is None:
            continue
        delta = rating - own_rating
        if mode == "rated":
            eligible = abs(delta) <= CLOSE_RATING
        elif mode == "training":
            eligible = TRAINING_MIN_ADVANTAGE <= delta <= TRAINING_MAX_ADVANTAGE
        else:
            raise ValueError("mode must be rated or training")
        if eligible:
            result.append((user, rating))
    return result


def utc_day():
    return datetime.now(timezone.utc).strftime("%Y-%m-%d")


def seconds_to_midnight():
    now = datetime.now(timezone.utc)
    midnight = (now + timedelta(days=1)).replace(hour=0, minute=0, second=5, microsecond=0)
    return max(5, int((midnight - now).total_seconds()))


def load_state(path):
    state = {"day": utc_day(), "games_today": 0, "games_since_training": 3,
             "recent_targets": {}, "last_attempt_ts": 0}
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(loaded, dict):
            state.update(loaded)
    except (OSError, ValueError):
        pass
    if state.get("day") != utc_day():
        state["day"] = utc_day()
        state["games_today"] = 0
    if not isinstance(state.get("recent_targets"), dict):
        state["recent_targets"] = {}
    return state


def save_state(path, state):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


class LichessAPI:
    def __init__(self, token, username):
        self.headers = {
            "Authorization": "Bearer " + token,
            "User-Agent": "Athena-Lichess-Matchmaker/1.0 (" + username + ")",
        }
        # urllib does not automatically use certifi's CA bundle. On some macOS
        # Python installs its default certificate paths are missing or outdated.
        self.ca_bundle = certifi.where() if certifi is not None else None
        self.ssl_context = (
            ssl.create_default_context(cafile=self.ca_bundle)
            if self.ca_bundle
            else ssl.create_default_context()
        )

    def request(self, method, path, data=None):
        url = API_BASE + path
        body = None
        headers = dict(self.headers)
        if data is not None:
            body = urlencode(data).encode("utf-8")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = Request(url, data=body, headers=headers, method=method)
        try:
            with urlopen(req, timeout=15, context=self.ssl_context) as response:
                raw = response.read().decode("utf-8", errors="replace")
                status = response.status
        except HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            status = exc.code
        except URLError as exc:
            if isinstance(exc.reason, ssl.SSLCertVerificationError):
                if self.ca_bundle:
                    advice = (
                        "The request was checked against certifi's CA bundle at "
                        + self.ca_bundle
                        + ". Update it with the same Python environment's "
                        + "'python -m pip install --upgrade certifi'. If verification "
                        + "still fails, check for a network proxy or local security "
                        + "software requiring its CA certificate to be trusted."
                    )
                else:
                    advice = (
                        "certifi is not installed in this Python environment. Run "
                        + "'python -m pip install --upgrade certifi' using the Python "
                        + "environment that launches Athena, then restart it."
                    )
                raise RuntimeError(
                    "TLS certificate verification failed for Lichess. " + advice
                ) from exc
            raise RuntimeError("Lichess request failed: " + str(exc)) from exc
        except (TimeoutError, OSError) as exc:
            raise RuntimeError("Lichess request failed: " + str(exc)) from exc
        try:
            parsed = json.loads(raw) if raw.strip() else {}
        except json.JSONDecodeError:
            parsed = raw  # The online-bots endpoint streams newline-delimited JSON.
        return status, parsed

    def get_account(self):
        status, data = self.request("GET", "/api/account")
        if status != 200 or not isinstance(data, dict):
            raise RuntimeError("Could not read bot account (HTTP " + str(status) + ").")
        return data

    def get_active_games(self):
        status, data = self.request("GET", "/api/account/playing")
        if status != 200 or not isinstance(data, dict):
            raise RuntimeError("Could not read active games (HTTP " + str(status) + ").")
        return data.get("nowPlaying", [])

    def get_online_bots(self):
        status, data = self.request("GET", "/api/bot/online?nb=512")
        if status != 200 or not isinstance(data, str):
            if status == 200 and isinstance(data, dict):
                return [data]
            raise RuntimeError("Could not list online bots (HTTP " + str(status) + ").")
        users = []
        for line in data.splitlines():
            try:
                parsed = json.loads(line)
                if isinstance(parsed, dict):
                    users.append(parsed)
            except json.JSONDecodeError:
                continue
        return users

    def challenge(self, username, base_time, increment, rated):
        return self.request("POST", "/api/challenge/" + username, {
            "rated": "true" if rated else "false",
            "variant": "standard",
            "clock.limit": str(base_time),
            "clock.increment": str(increment),
        })


def active_game_against(games, username):
    wanted = username.casefold()
    for game in games:
        opponent = game.get("opponent", {})
        if isinstance(opponent, dict) and opponent.get("username", "").casefold() == wanted:
            return True
    return False


def run(args):
    token = args.token or os.environ.get("LICHESS_BOT_TOKEN", "")
    if not token:
        raise SystemExit("ERROR: LICHESS_BOT_TOKEN is required for the matcher.")
    state_path = Path(args.state).expanduser()
    state = load_state(state_path)
    api = LichessAPI(token, args.username)
    print("Athena rating-aware matchmaking is running.", flush=True)

    while True:
        try:
            profile = api.get_account()
            username = profile.get("username") or args.username
            active = api.get_active_games()
            if active:
                print("Matchmaker paused: " + str(len(active)) + " active game(s).", flush=True)
                time.sleep(15)
                continue

            if state.get("day") != utc_day():
                state["day"] = utc_day()
                state["games_today"] = 0
            if int(state.get("games_today", 0)) >= MAX_ACCEPTED_GAMES_PER_UTC_DAY:
                print("Reached the daily safety cap; waiting for UTC midnight.", flush=True)
                time.sleep(min(seconds_to_midnight(), 3600))
                continue

            now = time.time()
            cooldown = CHALLENGE_COOLDOWN_SECONDS - (now - float(state.get("last_attempt_ts", 0)))
            if cooldown > 0:
                time.sleep(min(cooldown, 15))
                continue

            recent = {k: float(v) for k, v in state["recent_targets"].items()
                      if now - float(v) < RECENT_TARGET_SECONDS}
            state["recent_targets"] = recent
            bots = api.get_online_bots()
            choices = []
            for category, clocks in CONTROL_CHOICES.items():
                own_rating = rating_for(profile, category)
                if own_rating is None:
                    continue
                for user, rating in eligible_opponents(bots, username, category, own_rating, "rated", recent, now):
                    choices.append(("rated", category, clocks, user, rating, own_rating))
                for user, rating in eligible_opponents(bots, username, category, own_rating, "training", recent, now):
                    choices.append(("training", category, clocks, user, rating, own_rating))

            training = [choice for choice in choices if choice[0] == "training"]
            rated = [choice for choice in choices if choice[0] == "rated"]
            # Play a casual training game after at most three normal games, if possible.
            if int(state.get("games_since_training", 3)) >= 3 and training:
                selected = random.choice(training)
            elif rated:
                selected = random.choice(rated)
            elif training:
                selected = random.choice(training)
            else:
                print("No eligible bots online: waiting 60 seconds.", flush=True)
                time.sleep(60)
                continue

            mode, category, clocks, target, target_rating, own_rating = selected
            base_time, increment = random.choice(clocks)
            target_name = target.get("username", "")
            is_rated = mode == "rated"
            delta = target_rating - own_rating
            desc = "rated match within +/-150" if is_rated else "casual training +" + str(delta)
            print("Seeking " + desc + ": " + target_name + " (" + str(target_rating)
                  + ", Athena " + str(own_rating) + ", " + category + ", "
                  + str(base_time) + "+" + str(increment) + ").", flush=True)

            state["last_attempt_ts"] = time.time()
            save_state(state_path, state)
            status, response = api.challenge(target_name, base_time, increment, is_rated)
            challenge_id = response.get("id") if isinstance(response, dict) else None
            if status != 200 or not challenge_id:
                ratelimit = response.get("ratelimit", {}) if isinstance(response, dict) else {}
                wait = float(ratelimit.get("seconds", 0) or 0)
                if status == 429 or ratelimit.get("key") == "bot.vsBot.day":
                    wait = max(wait, 60)
                    print("Lichess rate limit (HTTP " + str(status) + "); cooling down " + str(int(wait)) + " seconds.", flush=True)
                    time.sleep(wait)
                else:
                    message = response.get("error", response) if isinstance(response, dict) else response
                    print("Challenge failed (HTTP " + str(status) + "): " + str(message), flush=True)
                    recent[target_name.casefold()] = time.time()
                    state["recent_targets"] = recent
                    save_state(state_path, state)
                    time.sleep(CHALLENGE_COOLDOWN_SECONDS)
                continue

            recent[target_name.casefold()] = time.time()
            state["recent_targets"] = recent
            save_state(state_path, state)
            print("Challenge created: " + str(challenge_id) + ". Waiting for acceptance.", flush=True)
            started = False
            deadline = time.time() + 30
            while time.time() < deadline:
                time.sleep(4)
                if active_game_against(api.get_active_games(), target_name):
                    started = True
                    break
            if not started:
                print("Challenge not accepted or expired; matcher will try another bot later.", flush=True)
                continue

            state["day"] = utc_day()
            state["games_today"] = int(state.get("games_today", 0)) + 1
            if mode == "training":
                state["games_since_training"] = 0
            else:
                state["games_since_training"] = int(state.get("games_since_training", 0)) + 1
            save_state(state_path, state)
            print("Game accepted. Daily safety count: " + str(state["games_today"])
                  + "/" + str(MAX_ACCEPTED_GAMES_PER_UTC_DAY) + ".", flush=True)

            while True:
                time.sleep(15)
                if not api.get_active_games():
                    break
            print("Game ended; matchmaking will resume after cooldown.", flush=True)

        except KeyboardInterrupt:
            print("Matchmaker stopped.", flush=True)
            return
        except Exception as exc:
            print("Matchmaker error: " + str(exc) + ". Retrying in 30 seconds.", flush=True)
            time.sleep(30)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--username", default="AAthena101")
    parser.add_argument("--state", default="game_records/athena_matchmaking_state.json")
    parser.add_argument("--token", default="")
    run(parser.parse_args())


if __name__ == "__main__":
    main()
