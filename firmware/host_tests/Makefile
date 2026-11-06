CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -ffp-contract=off
CORE := ../libraries/FrostCore/src
SRC := $(CORE)/psychro.cpp $(CORE)/trend.cpp $(CORE)/engine.cpp $(CORE)/packet.cpp

all: test_core replay_cli

test_core: test_core.cpp $(SRC) $(wildcard $(CORE)/*.h)
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(SRC) -lm

replay_cli: replay_cli.cpp $(SRC) $(wildcard $(CORE)/*.h)
	$(CXX) $(CXXFLAGS) -o $@ replay_cli.cpp $(SRC) -lm

test: test_core
	./test_core

clean:
	rm -f test_core replay_cli

.PHONY: all test clean
