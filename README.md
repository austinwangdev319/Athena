# Athena

A UCI chess engine in C++17 that plays gambits.

Load `athena` in any UCI interface (Cute Chess, Arena), or connect it to Lichess through [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot). Lichess's website does not load an arbitrary local engine binary directly; the separate `lichess-bot` process connects the engine to the Bot API.

## Options

| Option | Default | |
|---|---:|---|
| Hash | 64 | MB |
| Threads | 1 | |
| Ponder | false | |
| Move Overhead | 80 | ms |
| TimeUse | 80 | % of allotted time per move |
| OwnBook | true | |
| BookFile | | extra opening lines, see below |
| GambitRisk | 100 | max centipawn deficit the book may accept |
| Aggression | 165 | king-attack weight, 100 = neutral |
| Contempt | 25 | centipawns |

## Opening book

`book/athena_gambits.txt` lists one line per opening:

    <weight> <deficit_cp> <w|b> <uci moves from the start position>

The same lines are built into the engine. Set `BookFile` to add or replace lines without rebuilding.

## Run Athena on Lichess

Athena is an engine, not a Lichess account. To have it play on the live Lichess site, run the official `lichess-bot` connector on a computer or server that remains online.

1. **Use a dedicated Lichess bot account.** If the account has previously played games as a normal account, it cannot be converted to a bot account. Upgrading is irreversible; follow the [official bot-account instructions](https://github.com/lichess-bot-devs/lichess-bot/wiki/Upgrade-to-a-BOT-account).
2. Install the official [lichess-bot project](https://github.com/lichess-bot-devs/lichess-bot) using its [installation instructions](https://github.com/lichess-bot-devs/lichess-bot/wiki/How-to-Install). For example, put it beside this repository in a folder named `lichess-bot`, and install its Python dependencies in its virtual environment.
3. Create an OAuth token for the bot account with the **`bot:play`** scope using the [official token page](https://lichess.org/account/oauth/token/create?scopes[]=bot:play&description=lichess-bot). Do not put the token in source code or commit it. If a token has been pasted into a chat, log, or repository, revoke it and create a replacement.
4. From the Athena repository root, set the new token in your shell and start the bridge:

   ```bash
   export LICHESS_BOT_TOKEN='PASTE_YOUR_NEW_TOKEN_HERE'
   export LICHESS_BOT_DIR="$PWD/../lichess-bot"
   bash ./lichess/run.sh
   ```

   The launcher builds Athena, copies the executable into `lichess-bot/engines/athena`, places Athena's config template at `lichess-bot/config.yml`, and starts `lichess-bot`. The token is supplied through the environment and is not written into this repository. The launcher replaces the target `config.yml`, so back up any custom configuration there first. On macOS, `lichess/start.command` provides a double-click launcher and stores the token in Keychain after the first prompt.

5. Keep the process running. The bot accepts casual and rated challenges, can play up to three games simultaneously while reserving one slot for human challengers, and can issue matchmaking challenges while already playing. Lichess limits bot-vs-bot games to 100 per day.

Athena opens with `1.e4`; against `1...c5`, it plays the Wing Gambit `2.b4`. Clock targets are under one second in bullet, roughly 2–3 seconds in blitz, and around five seconds in longer games, subject to the remaining clock.

A local result-based tuner records PGNs and adjusts aggression/gambit risk from recent results. This is a transparent heuristic, not neural-network training or position-by-position learning. Its profile is stored in `athena_style.txt` and is not committed.

The first run also requires a working Python environment for the official `lichess-bot` project. Set `LICHESS_BOT_PYTHON` if its Python executable is not in `lichess-bot/.venv/bin/python` or `lichess-bot/venv/bin/python`.

## Troubleshooting

- **Authentication failure:** create a fresh token for the bot account with `bot:play`; export it as `LICHESS_BOT_TOKEN` in the same terminal that runs the launcher.
- **Account cannot use the Bot API:** follow the official irreversible bot-account upgrade process above.
- **Engine not found:** verify `make` and a C++17 compiler are installed; then run the launcher from the Athena repository.
- **Athena is online but does not initiate a game:** matchmaking only runs while `lichess-bot` is running and the bot is idle. Check the console output and the matchmaking settings in `lichess/config.yml`.
