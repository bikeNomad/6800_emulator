# SPDX-License-Identifier: MIT
#
# Host-side tests for the emulator control API and state machine.
# Needs only a native C compiler and pthreads; no Pico SDK.
#
#   make        build and run
#   make clean

SRC := ../../src
BUILD := build

# Real sources under test. They are copied next to the fakes so that their
# quoted #includes pick up the fake hardware headers instead of the real ones.
REAL := emulator.c emu_api.c simple_fsm.c cpu_state.c pc_actions.c \
	emulator.h emu_api.h emu_ring.h simple_fsm.h cpu_state.h pc_actions.h

CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	-fsanitize=address,undefined -I$(BUILD)
LDFLAGS := -pthread -fsanitize=address,undefined

.PHONY: all test clean
all: test

$(BUILD)/.staged: $(addprefix $(SRC)/,$(REAL)) $(shell find fakes -type f) fake_platform.c test_emu_api.c
	rm -rf $(BUILD)
	mkdir -p $(BUILD)
	cp -R fakes/. $(BUILD)/
	cp $(addprefix $(SRC)/,$(REAL)) $(BUILD)/
	cp fake_platform.c test_emu_api.c $(BUILD)/
	touch $@

$(BUILD)/test_emu_api: $(BUILD)/.staged
	$(CC) $(CFLAGS) -o $@ $(addprefix $(BUILD)/,$(filter %.c,$(REAL)) fake_platform.c test_emu_api.c) $(LDFLAGS)

test: $(BUILD)/test_emu_api
	./$(BUILD)/test_emu_api

clean:
	rm -rf $(BUILD)
