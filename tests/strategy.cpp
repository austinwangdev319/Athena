#include "../src/search.hpp"
#include <iostream>

using namespace ag;

static bool load(Position& p, const char* fen) {
    if (!p.setFen(fen)) {
        std::cerr << "Invalid test FEN: " << fen << "\n";
        return false;
    }
    return true;
}

int main() {
    Position quiet, imminent, blocked;
    if (!load(quiet, "7k/8/8/8/8/8/8/K7 w - - 0 1")) return 1;
    if (!load(imminent, "7k/8/8/8/8/8/3p4/K7 w - - 0 1")) return 1;
    if (!load(blocked, "7k/8/8/8/8/8/3p4/K2R4 w - - 0 1")) return 1;

    const int quietScore = evaluate(quiet);
    const int imminentScore = evaluate(imminent);
    const int blockedScore = evaluate(blocked);
    if (imminentScore > quietScore - 180) {
        std::cerr << "An unobstructed seventh-rank passed pawn was not treated as urgent: "
                  << quietScore << " vs " << imminentScore << "\n";
        return 1;
    }
    if (blockedScore < imminentScore + 300) {
        std::cerr << "The blocked passer was not distinguished from the promotion threat: "
                  << blockedScore << " vs " << imminentScore << "\n";
        return 1;
    }

    SearchShared shared;
    Worker worker(shared, 0);
    worker.pos = imminent;
    if (!worker.hasUrgentPassedPawn()) {
        std::cerr << "Search did not detect an urgent passed pawn.\n";
        return 1;
    }
    worker.pos = quiet;
    if (worker.hasUrgentPassedPawn()) {
        std::cerr << "Search flagged a pawn race when no pawns were present.\n";
        return 1;
    }
    std::cout << "Passed-pawn strategy tests passed. Scores quiet=" << quietScore
              << ", imminent=" << imminentScore << ", blocked=" << blockedScore << ".\n";
    return 0;
}
