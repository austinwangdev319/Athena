CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -pthread -DNDEBUG -Wall -Wextra
ARCH ?= -march=x86-64-v2

athena: src/main.cpp src/chess.hpp src/eval.hpp src/search.hpp src/book.hpp src/gambits.inc src/standard_book.inc
	$(CXX) $(CXXFLAGS) $(ARCH) -o $@ src/main.cpp

test: tests/perft.cpp tests/book.cpp tests/strategy.cpp src/chess.hpp src/eval.hpp src/search.hpp src/book.hpp src/gambits.inc src/standard_book.inc
	$(CXX) -O2 -std=c++17 -o perft_test tests/perft.cpp && ./perft_test
	$(CXX) -O2 -std=c++17 -pthread -o book_test tests/book.cpp && ./book_test
	$(CXX) -O2 -std=c++17 -pthread -o strategy_test tests/strategy.cpp && ./strategy_test
	python3 -m py_compile tools/matchmaking.py tools/learn_from_games.py tools/review_games.py tests/test_matchmaking.py tests/test_learner.py tests/test_review_games.py
	python3 -m unittest tests.test_matchmaking tests.test_learner tests.test_review_games

book:
	python3 tools/build_book.py

clean:
	rm -f athena perft_test book_test strategy_test

.PHONY: test book clean
