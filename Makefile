CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -pthread -DNDEBUG -Wall -Wextra
ARCH ?= -march=x86-64-v2

athena: src/main.cpp src/chess.hpp src/eval.hpp src/search.hpp src/book.hpp src/gambits.inc
	$(CXX) $(CXXFLAGS) $(ARCH) -o $@ src/main.cpp

test: tests/perft.cpp src/chess.hpp
	$(CXX) -O2 -std=c++17 -o perft_test tests/perft.cpp && ./perft_test

book:
	python3 tools/build_book.py

clean:
	rm -f athena perft_test

.PHONY: test book clean
