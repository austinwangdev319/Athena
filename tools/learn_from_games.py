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
    """Conservative outcome fallback. Move-level review is handled by review_games.py."""
    if not games:
        return 80, 30, "No completed games; using conservative defaults."

    adjusted = []
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

        own_key, opponent_key = ("WhiteElo", "BlackElo") if is_white else ("BlackElo", "WhiteElo")
        try:
            own_rating = int(game.get(own_key, ""))
            opponent_rating = int(game.get(opponent_key, ""))
            expected = expected_score(opponent_rating - own_rating) if own_rating > 0 and opponent_rating > 0 else 0.5
        except (TypeError, ValueError):
            expected = 0.5
        adjusted.append(actual - expected)

    if not adjusted:
        return 80, 30, "No games for the configured username; using conservative defaults."

    advantage = sum(adjusted) / len(adjusted)
    # Game outcomes alone are noisy. Never raise attack/gambit settings after wins;
    # only reduce them modestly when repeated results fall below rating expectation.
    if advantage <= -0.12:
        penalty = min(25, max(5, int(abs(advantage) * 30)))
        return 80 - penalty, max(5, 30 - penalty), (
            f"Recent performance below rating expectation by {abs(advantage):.2f}: "
            "reducing speculative risk. Detailed move review is still needed."
        )
    if advantage <= -0.03:
        return 75, 25, (
            f"Recent results slightly below rating expectation ({advantage:.2f}): "
            "using a modestly safer style."
        )
    return 80, 30, (
        f"Recent rating-adjusted results {advantage:+.2f}: preserving conservative settings; "
        "wins alone do not increase aggression."
    )

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
