#include "../src/book.hpp"
#include <iostream>
#include <random>
#include <unordered_set>

int main() {
    using namespace ag;
    const auto book = builtinBook();
    if (book.empty()) { std::cerr << "Opening book is empty\n"; return 1; }
    Position start;
    if (!start.setFen(Position::startFen())) { std::cerr << "Invalid start position\n"; return 1; }
    const auto legal = start.legal();
    std::mt19937_64 rng(20261016);
    std::unordered_set<std::string> firstMoves;
    for (int i = 0; i < 5000; ++i) {
        Move m = bookMove(start, book, 20, 150, rng);
        if (!m || std::find(legal.begin(), legal.end(), m) == legal.end()) {
            std::cerr << "Opening book returned an illegal root move\n"; return 1;
        }
        firstMoves.insert(m.uci());
    }
    for (const auto* required : {"e2e4", "d2d4", "g1f3", "c2c4"}) {
        if (!firstMoves.count(required)) {
            std::cerr << "Opening book did not sample expected first move " << required << "\n"; return 1;
        }
    }
    Position sicilian;
    sicilian.setFen(Position::startFen());
    for (const char* move : {"e2e4", "c7c5"}) {
        Move parsed = sicilian.parseMove(move);
        if (!parsed) { std::cerr << "Could not construct Sicilian test position\n"; return 1; }
        Undo undo;
        sicilian.make(parsed, undo);
    }
    Move wing = bookMove(sicilian, book, 20, 150, rng);
    if (!wing || wing.uci() != "b2b4") {
        std::cerr << "Athena did not preserve the Wing Gambit vs the Sicilian\n"; return 1;
    }
    std::cout << "Opening book test passed: " << book.size()
              << " valid lines; " << firstMoves.size() << " root moves sampled.\n";
    return 0;
}
