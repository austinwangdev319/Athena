#!/usr/bin/env bash
set -euo pipefail

ATHENA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOT_DIR="${LICHESS_BOT_DIR:-"$ATHENA_ROOT/../lichess-bot"}"

if [[ -z "${LICHESS_BOT_TOKEN:-" ]]; then
  echo "ERROR: Set LICHESS_BOT_TOKEN to a fresh Lichess bot:play token." >&2
  exit 2
fi

if [[ ! -f "$BOT_DIR/lichess-bot.py" ]]; then
  echo "ERROR: lichess-bot.py was not found in: $BOT_DIR" >&2
  echo "Clone/install https://github.com/lichess-bot-devs/lichess-bot, then set LICHESS_BOT_DIR." >&2
  exit 2
fi

if ! command -v make >/dev/null 2>&1; then
  echo "ERROR: make is required to build Athena." >&2
  exit 2
fi

echo "Building Athena..."
# Disable the Makefile's x86-specific default flags for better portability.
make -C "$ATHENA_ROOT" athena ARCH=

mkdir -p "$BOT_DIR/engines"
cp "$ATHENA_ROOT/athena" "$BOT_DIR/engines/athena"

# Fail early if the deployed engine differs from the binary just built.
if ! cmp -s "$ATHENA_ROOT/athena" "$BOT_DIR/engines/athena"; then
  echo "ERROR: The copied Athena engine does not match the build output." >&2
  exit 3
fi
echo "Verified engine binary copied to $BOT_DIR/engines/athena"

# The official runner reads LICHESS_BOT_TOKEN from the environment and
# overrides this harmless placeholder. No token is written into the config.
cp "$ATHENA_ROOT/lichess/config.yml" "$BOT_DIR/config.yml"

if [[ -n "${LICHESS_BOT_PYTHON:-" ]]; then
  BOT_PYTHON="${LICHESS_BOT_PYTHON"
elif [[ -x "$BOT_DIR/.venv/bin/python" ]]; then
  BOT_PYTHON="$BOT_DIR/.venv/bin/python"
elif [[ -x "$BOT_DIR/venv/bin/python" ]]; then
  BOT_PYTHON="$BOT_DIR/venv/bin/python"
else
  BOT_PYTHON="python3"
fi

# Fail before connecting if the configuration is malformed.
ATHENA_CONFIG="$BOT_DIR/config.yml" "$BOT_PYTHON" -c \
  'import os, yaml; yaml.safe_load(open(os.environ["ATHENA_CONFIG"], encoding="utf-8")); print("Verified Lichess bot YAML configuration")'

echo "Starting Athena through lichess-bot from $BOT_DIR"
mkdir -p "$BOT_DIR/game_records"

LEARNER_PID=""
MATCHMAKER_PID=""
cleanup() {
  if [[ -n "$LEARNER_PID" ]]; then kill "$LEARNER_PID" 2>/dev/null || true; fi
  if [[ -n "$MATCHMAKER_PID" ]]; then kill "$MATCHMAKER_PID" 2>/dev/null || true; fi
  if [[ -n "$LEARNER_PID" ]]; then wait "$LEARNER_PID" 2>/dev/null || true; fi
  if [[ -n "$MATCHMAKER_PID" ]]; then wait "$MATCHMAKER_PID" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

"$BOT_PYTHON" "$ATHENA_ROOT/tools/learn_from_games.py" \
  --watch --directory "$BOT_DIR/game_records" \
  --username "${ATHENA_LICHESS_USERNAME:-AAthena101}" \
  --output "$BOT_DIR/engines/athena_style.txt" &
LEARNER_PID=$!

"$BOT_PYTHON" "$ATHENA_ROOT/tools/matchmaking.py" \
  --username "${ATHENA_LICHESS_USERNAME:-AAthena101}" \
  --state "$BOT_DIR/game_records/athena_matchmaking_state.json" &
MATCHMAKER_PID=$!

cd "$BOT_DIR"
"$BOT_PYTHON" lichess-bot.py
