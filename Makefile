# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

PYTHON ?= python3
BUILD := build
NAME := openscex-modchip

# The only target: five pins for the four signals and the indicator; PB5 stays RESET.
MCU := attiny85
FLASH_BYTES := 8192
# Flash the image must leave free (text plus data), so the build fails while a
# fix still fits. 256 bytes is 3 percent of the part.
FLASH_RESERVE := 256
FLASH_BUDGET := $(shell expr $(FLASH_BYTES) - $(FLASH_RESERVE))

# Internal 8 MHz RC oscillator (CKSEL 0010, low fuse 0xE2), trimmed in firmware
# against the SUBQ frame rate. The documented fuses set brown-out at 2.7 V, the
# chip's minimum for 8 MHz (datasheet 2586Q, speed grade).
F_CPU := 8000000UL

# Video CD filter for the SCPH-5903: on keeps a Video CD from drawing a region
# string (PsNee V9.0 PSNee.ino:471-502). Off by default.
VCD_FILTER ?= off
ifeq ($(VCD_FILTER),on)
VCD_DEF := -DPSCU_VCD_FILTER=1
VCD_TAG := -vcd
else ifeq ($(VCD_FILTER),off)
VCD_DEF := -DPSCU_VCD_FILTER=0
VCD_TAG :=
else
$(error unknown VCD_FILTER '$(VCD_FILTER)'; use off or on)
endif

# Profile of PB3: debug (default) and final drive the LED or active buzzer
# (debug shows everything, final chirps at power-on and on acceptance and shows
# each fault once); trace sends every captured SUBQ frame to a serial adapter
# instead. DATA is the same in all three. See include/pscu/indicator.h.
PROFILE ?= debug
ifeq ($(PROFILE),final)
PROFILE_DEF := -DPSCU_PROFILE_FINAL=1 -DPSCU_PROFILE_TRACE=0
PROFILE_TAG := -final
else ifeq ($(PROFILE),debug)
PROFILE_DEF := -DPSCU_PROFILE_FINAL=0 -DPSCU_PROFILE_TRACE=0
PROFILE_TAG :=
else ifeq ($(PROFILE),trace)
PROFILE_DEF := -DPSCU_PROFILE_FINAL=0 -DPSCU_PROFILE_TRACE=1
PROFILE_TAG := -trace
else
$(error unknown PROFILE '$(PROFILE)'; use debug, final or trace)
endif

# Region string the chip emits. us is the default and carries no tag.
REGION ?= us
ifeq ($(REGION),jp)
REGION_DEF := -DPSCU_REGION_JP
REGION_TAG := -jp
else ifeq ($(REGION),eu)
REGION_DEF := -DPSCU_REGION_EU
REGION_TAG := -eu
else ifeq ($(REGION),us)
REGION_DEF :=
REGION_TAG :=
else
$(error unknown REGION '$(REGION)'; use jp, us or eu)
endif

# Separate object directories per region, filter and profile. The default
# build has an empty VARIANT and the plain artifact name the sim uses.
VARIANT := $(REGION_TAG)$(VCD_TAG)$(PROFILE_TAG)

CONTAINER_TARGETS := all size hosttest simtest analyse test misra repro mutate mutate_platform format \
	precommit image image_size image_misra bench_runners bench bench_ci showcase

.PHONY: $(CONTAINER_TARGETS) clean hooks

clean:
	rm -rf $(BUILD)

# Use the versioned hooks in .githooks: commit-msg checks the subject,
# pre-commit runs `make precommit`.
hooks:
	git config core.hooksPath .githooks

ifndef PSCU_TOOLCHAIN

$(CONTAINER_TARGETS):
	$(PYTHON) tools/docker_make.py $@ REGION=$(REGION) VCD_FILTER=$(VCD_FILTER) PROFILE=$(PROFILE)

else

HOST_CC := gcc
AVR_CC := avr-gcc
AVR_OBJCOPY := avr-objcopy
AVR_SIZE := avr-size

C_STD := -std=c17 -pedantic-errors
WARNINGS := -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wundef -Wcast-qual -Wswitch-enum \
	-Wswitch-default -Wdouble-promotion -Wnull-dereference -Wvla -Wredundant-decls -Wformat=2
HOST_CFLAGS := $(C_STD) -Iinclude $(WARNINGS)

LOGIC_C := src/region.c src/subq.c src/board_mode.c src/inject.c src/indicator.c src/calib.c src/trim.c src/supply.c src/loop.c
HOST_LOGIC_C := $(LOGIC_C)

FIRMWARE_C := $(LOGIC_C) src/engine.c src/run.c src/main.c
CPPCHECK_MCU_DEF := -D__AVR_ATtiny85__

ALL_SRC_C := $(FIRMWARE_C)
FIRMWARE_S := src/port.S src/port_chip.S
FIRMWARE_H := $(wildcard include/pscu/*.h) $(wildcard include/port/*.h)
HOST_TEST_C := tests/host/host_assert.c tests/host/host_test.c tests/host/calib_test.c tests/host/trim_test.c tests/host/disc_test.c tests/host/indicator_test.c tests/host/loop_test.c
SIM_TEST_C := tests/sim/sim_test.c tests/sim/harness.c tests/sim/sim_inject.c tests/sim/sim_disc.c tests/sim/sim_calib.c tests/sim/sim_profile.c tests/sim/sim_trace.c
SIM_TEST_H := tests/sim/harness.h tests/sim/scenarios.h tests/sim/sim_t85.h
BENCH_SRC := bench/pic_runner.cc bench/avr_runner.c
C_FILES := $(ALL_SRC_C) $(FIRMWARE_H) $(HOST_TEST_C) $(SIM_TEST_C) $(SIM_TEST_H) $(BENCH_SRC)
HOST_TEST := $(BUILD)/host/host_test
SIM_TEST := $(BUILD)/sim/sim_test
SIM_CFLAGS := $(C_STD) -O2 -Wall -Wextra -Werror \
	$(patsubst -I%,-isystem %,$(shell pkg-config --cflags simavr libelf))
SIM_LIBS := $(shell pkg-config --libs simavr libelf)

RELEASE := $(BUILD)/$(MCU)$(VARIANT)/release
RELEASE_ELF := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).elf
RELEASE_HEX := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).hex
RELEASE_OBJECTS := $(patsubst src/%,$(RELEASE)/%.o,$(FIRMWARE_C) $(FIRMWARE_S))

# -g adds a line table without changing the code; the bench uses it to map
# addresses to source lines. The link step needs it too, because of LTO.
AVR_DEBUG := -g
AVR_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(REGION_DEF) $(VCD_DEF) $(PROFILE_DEF) $(C_STD) -Os -flto -ffat-lto-objects -Iinclude \
	$(WARNINGS) -fno-common -ffunction-sections -fdata-sections $(AVR_DEBUG)
AVR_ASFLAGS := -mmcu=$(MCU) -x assembler-with-cpp -DF_CPU=$(F_CPU) $(PROFILE_DEF) -Iinclude -Wall -Wextra -Werror $(AVR_DEBUG)
AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections $(AVR_DEBUG)

AVR_INCLUDE := /usr/lib/avr/include
AVR_GCC_INCLUDE := $(shell $(AVR_CC) -print-file-name=include)
CPPCHECK_FLAGS := --std=c17 --platform=avr8 --enable=all --check-level=exhaustive \
	--error-exitcode=1 --suppress=checkersReport --inline-suppr \
	'--suppress=*:$(AVR_INCLUDE)/*' '--suppress=*:$(AVR_GCC_INCLUDE)/*' \
	-Iinclude -I$(AVR_INCLUDE) -I$(AVR_GCC_INCLUDE) \
	$(CPPCHECK_MCU_DEF) $(REGION_DEF) $(VCD_DEF) $(PROFILE_DEF) -DF_CPU=$(F_CPU)
CPPCHECK_CONFIGS := -DPSCU_DEBUG -UPSCU_DEBUG

all: image

image: $(RELEASE_HEX)

# Objects depend on this Makefile, since the build knobs are -D flags here.
$(RELEASE)/%.c.o: src/%.c $(FIRMWARE_H) Makefile
	@mkdir -p $(@D)
	$(AVR_CC) $(AVR_CFLAGS) -c -o $@ $<

$(RELEASE)/%.S.o: src/%.S include/port/registers.h Makefile
	@mkdir -p $(@D)
	$(AVR_CC) $(AVR_ASFLAGS) -c -o $@ $<

$(RELEASE_ELF): $(RELEASE_OBJECTS)
	$(AVR_CC) $(AVR_LDFLAGS) -o $@ $^

$(RELEASE_HEX): $(RELEASE_ELF)
	$(AVR_OBJCOPY) -O ihex -R .eeprom $< $@

# Every profile must fit: the final and trace images are sized with the
# default one.
size: image_size
	$(MAKE) --no-print-directory PROFILE=final image_size
	$(MAKE) --no-print-directory PROFILE=trace image_size

image_size: $(RELEASE_ELF)
	$(AVR_SIZE) $(RELEASE_ELF)
	@used=$$($(AVR_SIZE) $(RELEASE_ELF) | awk 'NR == 2 { print $$1 + $$2 }'); \
	if [ -z "$$used" ] || [ "$$used" -gt $(FLASH_BUDGET) ]; then \
	  echo "flash: $$used bytes, over the $(FLASH_BUDGET)-byte budget ($(FLASH_BYTES) less $(FLASH_RESERVE) reserved)" >&2; \
	  exit 1; \
	fi; \
	echo "flash: $$used of $(FLASH_BUDGET) budgeted bytes"

hosttest: $(HOST_TEST)
	rm -f $(BUILD)/host/*.gcda
	$(HOST_TEST)
	gcovr --root . --filter 'src/' --exclude-branches-by-pattern '.*PSCU_ASSERT.*' \
		--fail-under-line 100 --fail-under-branch 100 --print-summary $(BUILD)/host

$(HOST_TEST): $(HOST_LOGIC_C) $(HOST_TEST_C) $(FIRMWARE_H)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -O0 -DPSCU_DEBUG --coverage -o $@ $(HOST_LOGIC_C) $(HOST_TEST_C)

SIM_ELF := $(BUILD)/attiny85/release/$(NAME)-attiny85.elf
SIM_ELF_VCD := $(BUILD)/attiny85-jp-vcd/release/$(NAME)-attiny85-jp-vcd.elf
SIM_ELF_FINAL := $(BUILD)/attiny85-final/release/$(NAME)-attiny85-final.elf
SIM_ELF_TRACE := $(BUILD)/attiny85-trace/release/$(NAME)-attiny85-trace.elf
SIM_ELF_EU := $(BUILD)/attiny85-eu/release/$(NAME)-attiny85-eu.elf
SIM_ELF_JP := $(BUILD)/attiny85-jp/release/$(NAME)-attiny85-jp.elf

$(SIM_TEST): $(SIM_TEST_C) $(SIM_TEST_H) all
	@mkdir -p $(@D)
	$(HOST_CC) $(SIM_CFLAGS) -o $@ $(SIM_TEST_C) $(SIM_LIBS)

# The bench runners: AVR images on simavr, PIC images on gpsim. Library headers
# are system includes so their warnings stay out of -Werror.
BENCH_PIC := $(BUILD)/bench/pic_runner
BENCH_AVR := $(BUILD)/bench/avr_runner
BENCH_CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Werror \
	$(patsubst -I%,-isystem %,$(shell pkg-config --cflags gpsim glib-2.0))
BENCH_PIC_LIBS := $(shell pkg-config --libs gpsim glib-2.0) -lgpsim

$(BENCH_PIC): bench/pic_runner.cc
	@mkdir -p $(@D)
	g++ $(BENCH_CXXFLAGS) -o $@ $< $(BENCH_PIC_LIBS)

$(BENCH_AVR): bench/avr_runner.c tests/sim/sim_t85.h
	@mkdir -p $(@D)
	$(HOST_CC) $(SIM_CFLAGS) -Itests/sim -o $@ $< $(SIM_LIBS)

bench_runners: $(BENCH_PIC) $(BENCH_AVR)

# `bench` runs every chip through every scenario, skipping images not on this
# machine (artifacts.manifest.json). `bench_ci` runs ours against PsNee, built
# from its pinned commit. Both fail when ours leaves the field-proven envelope.
bench: all bench_runners
	$(PYTHON) -m tools.bench.cli

bench_ci: all bench_runners
	$(PYTHON) -m tools.bench.cli --chips ours,psnee-attiny85,psnee-atmega328p

# Copy the last full bench's showcase results and regenerate the README
# tables. Run only after a full bench with every image present.
showcase:
	$(PYTHON) -m tools.bench.showcase build/bench/showcase.json

# simavr ignores OSCCAL, so trim scenarios vary the console's rate instead and
# check direction, bounds and persistence. Every image is an argument: the
# Video CD, final and trace images, then the European and Japanese ones.
simtest: $(SIM_TEST)
	$(MAKE) --no-print-directory REGION=jp VCD_FILTER=on image
	$(MAKE) --no-print-directory PROFILE=final image
	$(MAKE) --no-print-directory PROFILE=trace image
	$(MAKE) --no-print-directory REGION=eu image
	$(MAKE) --no-print-directory REGION=jp image
	$(SIM_TEST) $(SIM_ELF) $(F_CPU) $(SIM_ELF_VCD) $(SIM_ELF_FINAL) $(SIM_ELF_TRACE) \
		$(SIM_ELF_EU) $(SIM_ELF_JP)

test: hosttest simtest

format:
	clang-format -i $(C_FILES)

# Formatting and Python lint only, for the pre-commit hook.
precommit:
	clang-format --dry-run --Werror $(C_FILES)
	ruff check
	ruff format --check

analyse: all bench_runners
	clang-format --dry-run --Werror $(C_FILES)
	ruff check
	ruff format --check
	mypy --strict tools tests
	yamllint -s .
	actionlint
	shellcheck .github/scripts/*.sh .githooks/commit-msg .githooks/pre-commit
	reuse lint
	$(MAKE) misra
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage run --branch --source=tools -m unittest discover -s tests -t . -p 'test_*.py'
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage report -m

# MISRA covers every profile, since each compiles different code.
misra: image_misra
	$(MAKE) --no-print-directory PROFILE=final image_misra
	$(MAKE) --no-print-directory PROFILE=trace image_misra

image_misra:
	$(foreach config,$(CPPCHECK_CONFIGS),cppcheck $(CPPCHECK_FLAGS) $(config) --addon=misra $(FIRMWARE_C) &&) true

REPRO_A := $(BUILD)/repro-a
REPRO_B := $(BUILD)/repro-b

repro:
	rm -rf $(REPRO_A) $(REPRO_B)
	$(MAKE) --no-print-directory BUILD=$(REPRO_A) image
	$(MAKE) --no-print-directory BUILD=$(REPRO_B) image
	cd $(REPRO_A) && find . -type f \( -name '*.hex' -o -name '*.elf' \) | sort | xargs sha256sum > $(CURDIR)/$(BUILD)/repro-a.sums
	cd $(REPRO_B) && find . -type f \( -name '*.hex' -o -name '*.elf' \) | sort | xargs sha256sum > $(CURDIR)/$(BUILD)/repro-b.sums
	diff $(BUILD)/repro-a.sums $(BUILD)/repro-b.sums
	@echo "reproducible build verified: identical artifacts across two fresh builds"

mutate:
	$(PYTHON) tools/mutate.py

# The platform C, run.c and engine.c, mutated and killed by the simulator: about
# a minute per mutant, so it runs as its own CI job rather than in the gate.
mutate_platform:
	$(PYTHON) tools/mutate.py --platform

endif
