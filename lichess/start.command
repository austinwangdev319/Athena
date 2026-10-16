#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVICE="athena-lichess-token"
BOT_DIR="${LICHESS_BOT_DIR:-"$ROOT/../lichess-bot"}"

if ! command -v security >/dev/null 2>&1; then
  echo "This convenience launcher is intended for macOS."
  exit 1
fi
TOKEN="$(security find-generic-password -s "$SERVICE" -w 2>/dev/null || true)"
if [[ -z "$TOKEN" ]]; then
  echo "Paste the Lichess bot:play token once. It will be stored in macOS Keychain, not in the repository."
  read -r -s -p "Lichess token: " TOKEN
  echo
  if [[ -z "$TOKEN" ]]; then echo "No token supplied."; exit 2; fi
  security add-generic-password -a "$USER" -s "$SERVICE" -w "$TOKEN" -U
fi
export LICHESS_BOT_TOKEN="$TOKEN"
export LICHESS_BOT_DIR="$BOT_DIR"
export ATHENA_LICHESS_USERNAME="${ATHENA_LICHESS_USERNAME:-AAthena101}"
cd "$ROOT"
bash "$ROOT/lichess/run.sh"
