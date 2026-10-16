# Athena

A UCI-compatible chess engine written in C++17, built to embrace aggressive attacks, unconventional gambits, and dynamic play.

Developed by Austin Wang, Athena aims to capture his playing style: creative, unpredictable, and always searching for an attacking opportunity. It prioritizes initiative over material, favors sharp tactical complications over passive positional maneuvering, and is willing to sacrifice pieces or pawns to expose the enemy king. Its opening repertoire emphasizes offbeat gambits and less conventional positions, seeking to steer games away from familiar theoretical territory and into situations where calculation and adaptability matter more than memorized opening lines.

Athena's playing style is shaped by configurable aggression and gambit-risk parameters, opening-book preferences, and time-control-aware search. It also incorporates a lightweight, results-based tuning system that analyzes recent PGN game records and adjusts its style parameters according to performance. Rather than training a neural network from scratch, Athena improves through iterative heuristic tuning: game results inform its risk tolerance, allowing it to experiment with different balances between attacking ambition and practical consistency.

Athena is designed to challenge conventional engine play not by outcalculating engines like Stockfish in raw tactical strength, but by pursuing a different strategic objective. Against opponents that excel at precise evaluation and long-term positional optimization, Athena seeks to create asymmetry early, complicate the position, and encourage unfamiliar tactical decisions. Its gambit-heavy repertoire and aggressive decision-making are intended to make games unpredictable, even when the underlying position may not objectively favor it.

Athena is still an evolving project, not a claim to superhuman playing strength. Its real ambition is to explore how far a deliberately unconventional playing style, combined with systematic testing and incremental tuning, can go against more established approaches to engine chess.
