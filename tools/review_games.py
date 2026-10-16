#!/usr/bin/env python3
"""Bounded post-game move review and conservative style adaptation for Athena.

This uses Athena itself to estimate move regret in completed PGNs. It is a
transparent heuristic feedback loop, not neural-network training: an analyzed
loss of evaluation lowers future aggression/risk settings, and good results
never automatically increase those settings.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

import chess
import chess.pgn

BASE_AGGRESSION = 80
BASE_GAMBIT_RISK = 30
MIN_AGGRESSION = 55
MIN_GAMBIT_RISK = 5
DEFAULT_NODES = 12000
DEFAULT_MAX_POSITIONS = 12
REVIEWED_GAME_LIMIT = 12
RESULTS = {"1-0", "0-1", "1/2-1/2"}


def score_from_uci_info(line):
    """Extract side-to-move centipawns from one UCI info line."""
    tokens = line.strip().split()
    if not tokens or tokens[0] != "info" or "score" not in tokens:
        return None
    i = tokens.index("score")
    if i + 2 >= len(tokens):
        return None
    kind = tokens[i + 1]
    try:
        value = int(tokens[i + 2])
    except ValueError:
        return None
    if "lowerbound" in tokens or "upperbound" in tokens:
        return None
    if kind == "cp":
        return value
    if kind == "mate":
        if value == 0:
            return 30000
        return (1 if value > 0 else -1) * (30000 - min(abs(value), 200) * 100)
    return None


def move_regret(before_score, after_score):
    """Approximate regret in cp; scores are from each position's side-to-move view.

    Since the player to move changes after the actual move, the two reported
    scores are added. Athena's fixed +12 tempo offset is removed.
    """
    if before_score is None or after_score is None:
        return None
    return max(0, int(before_score) + int(after_score) - 24)


def stable_game_id(path, game):
    moves = " ".join(move.uci() for move in game.mainline_moves())
    headers = "\x1f".join(
        str(game.headers.get(k, ""))
        for k in ("Site", "Date", "White", "Black", "Result", "FEN")
    )
    return hashlib.sha256((str(path.resolve()) + "\x1f" + headers + "\x1f" + moves).encode("utf-8")).hexdigest()


def read_finished_games(directory):
    """Return completed games in PGN files as (mtime, path, game, stable id)."""
    found = []
    for path in Path(directory).rglob("*.pgn"):
        try:
            stamp = path.stat().st_mtime
            with path.open("r", encoding="utf-8", errors="replace") as handle:
                while True:
                    try:
                        game = chess.pgn.read_game(handle)
                    except (ValueError, IndexError):
                        continue
                    if game is None:
                        break
                    if game.headers.get("Result") not in RESULTS:
                        continue
                    gid = stable_game_id(path, game)
                    found.append((stamp, str(path), game, gid))
        except OSError:
            continue
    found.sort(key=lambda item: (item[0], item[1], item[3]))
    return found


def candidate_moves(game, username, max_positions=DEFAULT_MAX_POSITIONS):
    """Collect a spread of Athena decisions, emphasizing pawns and tactics."""
    white = game.headers.get("White", "").casefold()
    black = game.headers.get("Black", "").casefold()
    username = username.casefold()
    if white == username:
        athena_color = chess.WHITE
    elif black == username:
        athena_color = chess.BLACK
    else:
        return []

    board = game.board()
    candidates = []
    for ply, move in enumerate(game.mainline_moves()):
        mover_is_athena = board.turn == athena_color
        piece = board.piece_at(move.from_square)
        if mover_is_athena and piece is not None:
            is_capture = board.is_capture(move)
            san = board.san(move)
            before_fen = board.fen()
            after_board = board.copy(stack=False)
            after_board.push(move)
            gives_check = after_board.is_check()
            exposed_destination = (
                after_board.piece_at(move.to_square) is not None
                and after_board.is_attacked_by(after_board.turn, move.to_square)
            )
            important = (
                piece.piece_type == chess.PAWN
                or is_capture
                or gives_check
                or exposed_destination
                or ply % 6 == 0
            )
            if important:
                candidates.append({
                    "ply": ply + 1,
                    "fen": before_fen,
                    "after_fen": after_board.fen(),
                    "uci": move.uci(),
                    "san": san,
                    "pawn_move": piece.piece_type == chess.PAWN,
                    "capture": is_capture,
                    "check": gives_check,
                    "destination_attacked": exposed_destination,
                })
        board.push(move)

    if len(candidates) <= max_positions:
        return candidates
    if max_positions <= 1:
        return [candidates[-1]]
    # Deterministic spread across the game, avoiding a bias toward the opening.
    indexes = sorted({
        round(i * (len(candidates) - 1) / (max_positions - 1))
        for i in range(max_positions)
    })
    return [candidates[i] for i in indexes]


class UCIAnalyzer:
    """A single reusable engine process with a bounded node budget per position."""
    def __init__(self, engine_path, nodes=DEFAULT_NODES):
        self.nodes = max(1000, int(nodes))
        self.process = subprocess.Popen(
            [str(Path(engine_path).expanduser().resolve())],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._send("uci")
        self._wait_for("uciok")
        self._send("setoption name OwnBook value false")
        self._send("setoption name Aggression value " + str(BASE_AGGRESSION))
        self._send("setoption name GambitRisk value " + str(BASE_GAMBIT_RISK))
        self._send("setoption name Contempt value 0")
        self._send("isready")
        self._wait_for("readyok")

    def _send(self, command):
        if self.process.poll() is not None:
            raise RuntimeError("Athena analysis process has exited.")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def _wait_for(self, marker):
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("Athena exited while waiting for " + marker)
            if line.strip() == marker:
                return

    def score_fen(self, fen):
        self._send("position fen " + fen)
        self._send("go nodes " + str(self.nodes))
        best_score = None
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("Athena exited during position review.")
            if line.startswith("info "):
                parsed = score_from_uci_info(line)
                if parsed is not None:
                    best_score = parsed
            if line.startswith("bestmove "):
                return best_score

    def close(self):
        if self.process.poll() is None:
            try:
                self._send("quit")
                self.process.wait(timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()


def analyze_candidates(candidates, analyzer):
    reviewed = []
    for move in candidates:
        before = analyzer.score_fen(move["fen"])
        after = analyzer.score_fen(move["after_fen"])
        regret = move_regret(before, after)
        item = dict(move)
        item["best_score_before_cp"] = before
        item["played_position_score_cp"] = after
        item["regret_cp"] = regret
        item["classification"] = (
            "unscored" if regret is None else
            "blunder" if regret >= 400 else
            "mistake" if regret >= 250 else
            "inaccuracy" if regret >= 150 else
            "notable-error-free"
        )
        reviewed.append(item)
    return reviewed


def derive_style(reviews):
    """Only repeated engine-reviewed errors make settings more conservative."""
    moves = [
        move for game in reviews[-REVIEWED_GAME_LIMIT:]
        for move in game.get("moves", [])
        if isinstance(move.get("regret_cp"), int)
    ]
    major_errors = sum(move["regret_cp"] >= 250 for move in moves)
    blunders = sum(move["regret_cp"] >= 400 for move in moves)
    pawn_errors = sum(move.get("pawn_move", False) and move["regret_cp"] >= 180 for move in moves)

    aggression_penalty = min(25, major_errors * 2 + blunders * 2)
    risk_penalty = min(25, pawn_errors * 3 + blunders * 2)
    aggression = max(MIN_AGGRESSION, BASE_AGGRESSION - aggression_penalty)
    gambit_risk = max(MIN_GAMBIT_RISK, BASE_GAMBIT_RISK - risk_penalty)
    return aggression, gambit_risk, {
        "reviewed_moves": len(moves),
        "major_errors": major_errors,
        "blunders": blunders,
        "pawn_errors": pawn_errors,
        "aggression_penalty": aggression_penalty,
        "risk_penalty": risk_penalty,
    }


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def write_style(path, reviews):
    aggression, gambit_risk, stats = derive_style(reviews)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(
        "aggression " + str(aggression) + "\ngambit_risk " + str(gambit_risk) + "\n",
        encoding="utf-8",
    )
    temp.replace(path)
    return aggression, gambit_risk, stats


def load_state(path):
    path = Path(path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            return data
    except (OSError, ValueError):
        pass
    return {"initialized": False, "reviewed_ids": [], "reviews": []}


def review_once(args, state, analyzer=None):
    games = read_finished_games(args.directory)
    if not state.get("initialized", False):
        # Do not unexpectedly spend hours reanalyzing a large existing archive.
        # Bootstrap only the most recent three completed games.
        for _, _, _, gid in games[:-3]:
            state["reviewed_ids"].append(gid)
        state["initialized"] = True

    reviewed_ids = set(state.get("reviewed_ids", []))
    pending = [item for item in games if item[3] not in reviewed_ids]
    if not pending:
        aggression, risk, stats = write_style(args.output, state.get("reviews", []))
        atomic_json(args.state, state)
        print("No new finished games to review. Athena style: aggression "
              + str(aggression) + ", gambit risk " + str(risk) + ".", flush=True)
        return False

    stamp, path, game, gid = pending[-1]
    candidates = candidate_moves(game, args.username, args.max_positions)
    local_analyzer = analyzer
    owns_analyzer = False
    try:
        if local_analyzer is None:
            local_analyzer = UCIAnalyzer(args.engine, args.nodes)
            owns_analyzer = True
        moves = analyze_candidates(candidates, local_analyzer)
    except (OSError, RuntimeError, ValueError) as exc:
        print("Game review failed; keeping previous style: " + str(exc), flush=True)
        return False
    finally:
        if owns_analyzer and local_analyzer is not None:
            local_analyzer.close()

    notables = [m for m in moves if isinstance(m.get("regret_cp"), int) and m["regret_cp"] >= 250]
    pawn_errors = [m for m in moves if m.get("pawn_move") and isinstance(m.get("regret_cp"), int) and m["regret_cp"] >= 180]
    review = {
        "game_id": gid,
        "pgn": path,
        "date": game.headers.get("Date", ""),
        "white": game.headers.get("White", ""),
        "black": game.headers.get("Black", ""),
        "result": game.headers.get("Result", ""),
        "analyzed_positions": len(moves),
        "notable_errors": len(notables),
        "pawn_errors": len(pawn_errors),
        "worst_regret_cp": max((m["regret_cp"] for m in moves if isinstance(m.get("regret_cp"), int)), default=0),
        "moves": moves,
    }
    state.setdefault("reviews", []).append(review)
    state["reviews"] = state["reviews"][-REVIEWED_GAME_LIMIT:]
    state.setdefault("reviewed_ids", []).append(gid)
    state["reviewed_ids"] = state["reviewed_ids"][-2000:]
    aggression, risk, stats = write_style(args.output, state["reviews"])
    atomic_json(args.state, state)
    atomic_json(args.report, {"style": {"aggression": aggression, "gambit_risk": risk},
                              "aggregate": stats, "latest_game": review})
    print(
        "Reviewed " + path + " (" + str(len(moves)) + " Athena decisions, "
        + str(len(notables)) + " estimated mistakes/blunders, "
        + str(len(pawn_errors)) + " costly pawn moves). "
        + "Style now aggression=" + str(aggression) + ", gambit_risk=" + str(risk) + ".",
        flush=True,
    )
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    parser.add_argument("--username", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--report", default="game_records/athena_review_report.json")
    parser.add_argument("--nodes", type=int, default=DEFAULT_NODES)
    parser.add_argument("--max-positions", type=int, default=DEFAULT_MAX_POSITIONS)
    parser.add_argument("--interval", type=int, default=300)
    parser.add_argument("--watch", action="store_true")
    args = parser.parse_args()

    state = load_state(args.state)
    while True:
        try:
            review_once(args, state)
        except KeyboardInterrupt:
            print("Athena review stopped.", flush=True)
            return
        except Exception as exc:
            print("Athena review error: " + str(exc) + ". Keeping previous style.", flush=True)
        if not args.watch:
            break
        time.sleep(max(60, args.interval))


if __name__ == "__main__":
    main()
