CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?=
CPPFLAGS += -Iinclude

ifeq ($(shell uname -s),Darwin)
MACOS_SDK := $(shell xcrun --show-sdk-path)
CPPFLAGS += -isysroot $(MACOS_SDK) -isystem $(MACOS_SDK)/usr/include/c++/v1
LDFLAGS += -isysroot $(MACOS_SDK)
endif

BUILD_DIR := build
OBJECTS := $(BUILD_DIR)/solver.o $(BUILD_DIR)/dimacs.o $(BUILD_DIR)/proof.o
TOOL := $(BUILD_DIR)/satsolve
TEST := $(BUILD_DIR)/test_sat
DRAT := $(BUILD_DIR)/drat-trim

.PHONY: all test proof-test benchmark ablation clean drat-trim

all: $(TOOL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp include/sat/*.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(TOOL): src/main.cpp $(OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(TEST): tests/test_sat.cpp $(OBJECTS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

# drat-trim is the independent checker. It is fetched rather than vendored so
# it is unambiguously not my code doing the verifying.
$(DRAT): third_party/drat-trim.c | $(BUILD_DIR)
	$(CC) -O2 -w -o $@ $<

third_party/drat-trim.c:
	curl -sSL -o $@ https://raw.githubusercontent.com/marijnheule/drat-trim/master/drat-trim.c

drat-trim: $(DRAT)

test: $(TEST)
	./$(TEST)

proof-test: $(TOOL) $(DRAT)
	python3 scripts/check_proofs.py --solver ./$(TOOL) --drat ./$(DRAT) \
		--count $${PROOF_COUNT:-60}

benchmark: $(TOOL)
	python3 bench/benchmark.py --solver ./$(TOOL)

ablation: $(TOOL)
	python3 bench/ablation.py --solver ./$(TOOL)

clean:
	rm -rf $(BUILD_DIR)
