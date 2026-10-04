# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

PYTHON ?= python3
BUILD := build

CONTAINER_TARGETS := hosttest analyse test misra

.PHONY: $(CONTAINER_TARGETS) clean

clean:
	rm -rf $(BUILD)

ifndef PSCU_TOOLCHAIN

$(CONTAINER_TARGETS):
	$(PYTHON) tools/docker_make.py $@

else

HOST_CC := gcc
C_STD := -std=c17 -pedantic-errors
WARNINGS := -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wundef -Wcast-qual -Wswitch-enum \
	-Wswitch-default -Wdouble-promotion -Wnull-dereference -Wvla -Wredundant-decls -Wformat=2
HOST_CFLAGS := $(C_STD) -Iinclude $(WARNINGS)

LOGIC_C := src/region.c src/subq.c src/board_mode.c src/inject.c
LOGIC_H := $(wildcard include/pscu/*.h)
HOST_TEST_C := tests/host/host_assert.c tests/host/host_test.c
C_FILES := $(LOGIC_C) $(LOGIC_H) $(HOST_TEST_C)
HOST_TEST := $(BUILD)/host/host_test

CPPCHECK_FLAGS := --std=c17 --platform=avr8 --enable=all --check-level=exhaustive \
	--error-exitcode=1 --suppress=checkersReport --inline-suppr -Iinclude -D__AVR_ATtiny85__
CPPCHECK_CONFIGS := -DPSCU_DEBUG -UPSCU_DEBUG

hosttest: $(HOST_TEST)
	rm -f $(BUILD)/host/*.gcda
	$(HOST_TEST)
	gcovr --root . --filter 'src/' --exclude-branches-by-pattern '.*PSCU_ASSERT.*' \
		--fail-under-line 100 --fail-under-branch 100 --print-summary $(BUILD)/host

$(HOST_TEST): $(LOGIC_C) $(HOST_TEST_C) $(LOGIC_H)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -O0 -DPSCU_DEBUG --coverage -o $@ $(LOGIC_C) $(HOST_TEST_C)

test: hosttest

analyse:
	clang-format --dry-run --Werror $(C_FILES)
	ruff check
	ruff format --check
	reuse lint
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage run --branch --source=tools -m unittest discover -s tests -t . -p 'test_*.py'
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage report -m

misra:
	$(foreach config,$(CPPCHECK_CONFIGS),cppcheck $(CPPCHECK_FLAGS) $(config) --addon=misra $(LOGIC_C) &&) true

endif
