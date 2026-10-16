#!/usr/bin/env bash
set -euo pipefail

ATHENA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOT_DIR="${LICHESS_BOT_DIR:-"$ATHENA_ROOT/../lichess-bot"}"

if [[ -z "${LICHESS_BOT_TOKEN:-}" ]]; then
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

if [[ -n "${LICHESS_BOT_PYTHON:-}" ]]; then
  BOT_PYTHON="$LICHESS_BOT_PYTHON"
elif [[ -x "$BOT_DIR/.venv/bin/python" ]]; then
  BOT_PYTHON="$BOT_DIR/.venv/bin/python"
elif [[ -x "$BOT_DIR/venv/bin/python" ]]; then
  BOT_PYTHON="$BOT_DIR/venv/bin/python"
else
  BOT_PYTHON="python3"
fi

echo "Starting Athena through lichess-bot from $BOT_DIR"
mkdir -p "$BOT_DIR/game_records"
if command -v python3 >/dev/null 2>&1; then
  "$BOT_PYTHON" "$ATHENA_ROOT/tools/learn_from_games.py" --watch --directory "$BOT_DIR/game_records" --username "${ATHENA_LICHESS_USERNAME:-AAthena101}" --output "$BOT_DIR/engines/athena_style.txt" &
fi
cd "$BOT_DIR"
exec "$BOT_PYTHON" lichess-bot.py
