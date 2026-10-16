#!/usr/bin/env python3
"""Transparent results-based style tuning, adjusted for opponent rating (not neural-network training)."""
import argparse
import re
import time
from pathlib import Path

HEADER = re.compile(r'^\[(White|Black|WhiteElo|BlackElo|Result|Date|Event|Site)\s+"(.*)"\]$', re.M)


def read_games(directory):
    games = {}
    for path in Path(directory).rglob("*.pgn"):
        try:
            raw = path.read_text(errors="ignore")
        except OSError:
            continue
        # PGN files may contain multiple games.
        chunks = re.split(r'(?=^\[Event\s+")', raw, flags=re.M)
        for chunk in chunks:
            fields = dict(HEADER.findall(chunk))
            if not all(k in fields for k in ("White", "Black", "Result")):
                continue
            result = fields["Result"]
            if result not in ("1-0", "0-1", "1/2-1/2"):
                continue
            key = (fields.get("Date", ""), fields.get("Event", ""),
                   fields["White"], fields["Black"], result, fields.get("Site", ""))
            try:
                stamp = path.stat().st_mtime
            except OSError:
                stamp = 0
            games[key] = (stamp, fields)
    return sorted(games.values(), key=lambda item: item[0])[-20:]


def expected_score(rating_difference):
    """Expected score when the opponent is this many rating points higher."""
    return 1.0 / (1.0 + 10.0 ** (rating_difference / 400.0))


def tune(games, username):
    if not games:
        return 160, 90, "No finished games found yet; using aggressive defaults."

    adjusted = []
    raw_points = []
    for _, game in games:
        white = game["White"].casefold()
        black = game["Black"].casefold()
        is_white = username.casefold() == white
        is_black = username.casefold() == black
        if not is_white and not is_black:
            continue

        result = game["Result"]
        actual = 1.0 if result == "1-0" else 0.0 if result == "0-1" else 0.5
        if is_black:
            actual = 1.0 - actual
        raw_points.append(actual)

        own_key, opponent_key = ("WhiteElo", "BlackElo") if is_white else ("BlackElo", "WhiteElo")
        try:
            own_rating = int(game.get(own_key, ""))
            opponent_rating = int(game.get(opponent_key, ""))
            expected = expected_score(opponent_rating - own_rating) if own_rating > 0 and opponent_rating > 0 else 0.5
        except (TypeError, ValueError):
            expected = 0.5

        # A loss to a bot rated much higher is less informative than a loss to an equal-rated opponent.
        adjusted.append(actual - expected)

    if not adjusted:
        return 160, 90, "No games for the configured username; using aggressive defaults."

    advantage = sum(adjusted) / len(adjusted)
    rate = sum(raw_points) / len(raw_points)
    if advantage <= -0.12:
        return 125, 60, f"Recent score {rate:.0%}; below rating expectation by {abs(advantage):.2f}: reducing speculative risk."
    if advantage >= 0.15:
        return 175, 110, f"Recent score {rate:.0%}; above rating expectation by {advantage:.2f}: increasing initiative."
    return 155, 85, f"Recent score {rate:.0%}; performance near expectation: keeping a balanced attacking style."


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    parser.add_argument("--username", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--watch", action="store_true")
    args = parser.parse_args()
    output = Path(args.output)

    while True:
        games = read_games(args.directory)
        aggression, gambit_risk, note = tune(games, args.username)
        output.parent.mkdir(parents=True, exist_ok=True)
        temp = output.with_suffix(output.suffix + ".tmp")
        temp.write_text(f"aggression {aggression}\ngambit_risk {gambit_risk}\n", encoding="utf-8")
        temp.replace(output)
        print(f"Athena style: {note} ({len(games)} recent games scanned)", flush=True)
        if not args.watch:
            break
        time.sleep(60)


if __name__ == "__main__":
    main()
