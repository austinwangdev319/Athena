#!/usr/bin/env python3
"""Small, transparent result-based style tuner for Athena (not neural-network training)."""
import argparse
import re
import time
from pathlib import Path

HEADER = re.compile(r'^\[(White|Black|Result|Date|Event|Site)\s+"(.*)"\]$', re.M)

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

def tune(games, username):
    if not games:
        return 160, 90, "No finished games found yet; using aggressive defaults."
    points = []
    for _, game in games:
        white = game["White"].casefold()
        black = game["Black"].casefold()
        if username.casefold() == white:
            points.append(1.0 if game["Result"] == "1-0" else 0.0 if game["Result"] == "0-1" else 0.5)
        elif username.casefold() == black:
            points.append(1.0 if game["Result"] == "0-1" else 0.0 if game["Result"] == "1-0" else 0.5)
    if not points:
        return 160, 90, "No games for the configured username; using aggressive defaults."
    rate = sum(points) / len(points)
    if rate <= 0.30:
        return 120, 50, f"Recent score {rate:.0%}: reducing gambit risk and playing more conservatively."
    if rate >= 0.68:
        return 180, 115, f"Recent score {rate:.0%}: increasing initiative and gambit appetite."
    return 155, 85, f"Recent score {rate:.0%}: keeping a balanced attacking style."

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--directory", required=True)
    ap.add_argument("--username", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--watch", action="store_true")
    args = ap.parse_args()
    out = Path(args.output)
    while True:
        games = read_games(args.directory)
        aggression, gambit_risk, note = tune(games, args.username)
        out.parent.mkdir(parents=True, exist_ok=True)
        tmp = out.with_suffix(out.suffix + ".tmp")
        tmp.write_text(f"aggression {aggression}\ngambit_risk {gambit_risk}\n")
        tmp.replace(out)
        print(f"Athena style: {note} ({len(games)} recent games scanned)", flush=True)
        if not args.watch:
            break
        time.sleep(60)

if __name__ == "__main__":
    main()
