# Athena

A UCI chess engine in C++17 that plays gambits.

## Build

    make

## Run

Load `athena` in any UCI interface (Cute Chess, Arena), or use it with [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot) via `lichess/config.yml`.

## Options

| Option | Default | |
|---|---|---|
| Hash | 64 | MB |
| Threads | 1 | |
| Ponder | false | |
| Move Overhead | 60 | ms |
| TimeUse | 130 | % of allotted time per move |
| OwnBook | true | |
| BookFile | | extra opening lines, see below |
| GambitRisk | 60 | max centipawn deficit the book may accept |
| Aggression | 80 | king-attack weight, 100 = neutral |
| Contempt | 25 | centipawns |

## Opening book

`book/athena_gambits.txt` lists one line per opening:

    <weight> <deficit_cp> <w|b> <uci moves from the start position>

The same lines are built into the engine. Set `BookFile` to add or replace lines without rebuilding.

## Test

    make test

## License

MIT
