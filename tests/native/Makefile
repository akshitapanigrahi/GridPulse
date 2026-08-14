# GRID PULSE - native (host-compiled) tests for the pure firmware logic.
#
# WHY A PLAIN MAKEFILE IS THE PRIMARY PATH
#   ./run.sh --test must work on a grader's machine with no setup. A C++ compiler and
#   make are present by default on macOS with the Command Line Tools and on any Linux
#   with build-essential; CMake is not. firmware/CMakeLists.txt defines an equivalent
#   CTest target over the same sources for anyone who is building the firmware
#   anyway, but nothing in the test path requires it.
#
# WHAT IS BEING TESTED
#   Only firmware/src/pure/ - the translation units with no Pico SDK dependency.
#   That separation is what makes host compilation possible, and it is enforced by
#   the `check-purity` target below rather than left to discipline.
#
# Usage:
#   make            build and run
#   make build      build only
#   make check-purity
#   make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -Wshadow -Wconversion \
            -Wsign-conversion -Wold-style-cast -Wpedantic

# Emit a .d file per object listing the headers it included, so editing a header
# rebuilds everything that depends on it. Without this, a change to config.h or
# board_map.h leaves stale objects linked against the old constants - which shows up
# as tests failing against values nobody can find in the source.
DEPFLAGS := -MMD -MP

ROOT     := ../..
PURE     := $(ROOT)/firmware/src/pure
BUILD    := obj
BIN      := $(BUILD)/gridpulse_native_tests

# Production sources under test.
PURE_SRCS := \
	$(PURE)/rng.cpp \
	$(PURE)/scoring.cpp \
	$(PURE)/crc.cpp \
	$(PURE)/protocol.cpp \
	$(PURE)/healthmask.cpp \
	$(PURE)/fsm.cpp \
	$(PURE)/event.cpp \
	$(PURE)/runevents.cpp \
	$(PURE)/keyscan.cpp \
	$(PURE)/selftest.cpp

TEST_SRCS := \
	test_main.cpp \
	test_rng.cpp \
	test_scoring.cpp \
	test_crc.cpp \
	test_protocol.cpp \
	test_boardmap.cpp \
	test_healthmask.cpp \
	test_fsm.cpp \
	test_runevents.cpp \
	test_keyscan.cpp \
	test_selftest.cpp \
	test_queue_event.cpp

SRCS := $(PURE_SRCS) $(TEST_SRCS)
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(SRCS)))

VPATH := $(PURE):.

.PHONY: all build run check-purity clean

all: run

build: $(BIN)

run: check-purity $(BIN)
	@$(BIN)

$(BIN): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp | $(BUILD)
	@$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

# The purity rule: nothing in firmware/src/pure/ may include a Pico SDK header.
#
# This is what lets the game logic be compiled and tested on the host at all. It is
# checked mechanically because it is easy to break with a single convenient include
# and the failure would only show up as "the native tests no longer build", months
# later and far from the cause.
check-purity:
	@if grep -rn -E '#include[[:space:]]*[<"](pico/|hardware/|tusb|bsp/)' $(PURE) ; then \
		echo ""; \
		echo "ERROR: firmware/src/pure/ must not depend on the Pico SDK."; \
		echo "Move the SDK-dependent code into firmware/src/hal/ instead."; \
		exit 1; \
	fi
	@echo "purity check OK: firmware/src/pure/ has no Pico SDK dependency"

clean:
	@rm -rf $(BUILD)

-include $(OBJS:.o=.d)
