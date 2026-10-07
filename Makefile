# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

PYTHON ?= python3
BUILD := build
NAME := openscex-modchip

# The ATtiny84 is the only target: its spare pins carry the mandatory lid line
# without displacing the LED, which the 8-pin ATtiny85 cannot do.
MCU := attiny84
FLASH_BYTES := 8192

# The chip runs from its internal 8 MHz RC oscillator (CKSEL 0010, low fuse
# 0xE2), so it needs no clock wire and can be reprogrammed off the console. The
# factory calibration is guaranteed to +-10 percent and user calibration reaches
# +-1 percent (Read: ATtiny24A/44A/84A datasheet DS40002269A, Table 20-2); the
# firmware trims OSCCAL against the SUBQ frame rate, which the console's crystal
# sets. PsNee ships on the same factory-calibrated internal oscillator. 8 MHz
# needs about 2.4 V by the speed grade (0-4 MHz from 1.8 V, 0-10 MHz from 2.7
# V), so the documented fuses enable brown-out detection at 2.7 V.
F_CPU := 8000000UL

# Injection bit timing. adaptive (default) locks the WFCK-carrier injection bit
# to the console's own timing by counting WFCK periods, so the modern-board bit cell is
# immune to the MCU RC oscillator drifting; legacy boards keep the MCU delay
# because WFCK is static there and offers nothing to lock to. fixed restores the
# original behaviour where every bit cell is a compile-time MCU delay; that is
# the build whose timing was exercised on hardware. The adaptive default is
# verified in simulation only until a console retests it.
# PSCU_WFCK_PERIODS_PER_BIT is 30, Read from PsNee PerformInjectionSequence
# ("modulated across 30 WFCK edges"), about 4 ms at the 7.3 kHz init rate.
TIMING ?= adaptive
ifeq ($(TIMING),fixed)
TIMING_DEF := -DPSCU_TIMING_ADAPTIVE=0
TIMING_TAG := -fixed
else
TIMING_DEF := -DPSCU_TIMING_ADAPTIVE=1 -DPSCU_WFCK_PERIODS_PER_BIT=30
TIMING_TAG :=
endif

# Video-CD filter for the SCPH-5903, the Asian dual-interface model that also
# plays Video CDs. on narrows the lead-in match to the TOC markers A0..A2 and
# rejects the Video-CD lead-in pattern, so a Video CD never draws a region
# string; off (default) is the filter every other console uses. Read: PsNee V9.0
# PSNee.ino:471-502, its SCPH_5903 variant.
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

# Region the chip emulates. The build is region-specific so the firmware emits
# only the console's own region string. us is the default and carries no tag.
REGION ?= us
ifeq ($(REGION),jp)
REGION_DEF := -DPSCU_REGION_JP
REGION_TAG := -jp
else ifeq ($(REGION),eu)
REGION_DEF := -DPSCU_REGION_EU
REGION_TAG := -eu
else
REGION_DEF :=
REGION_TAG :=
endif

# Each region, timing and filter build differs in generated code, so their
# objects must never share a directory; VARIANT keeps them separate. An empty
# VARIANT (America, adaptive timing, no filter) keeps the plain artifact name
# the sim uses.
VARIANT := $(REGION_TAG)$(TIMING_TAG)$(VCD_TAG)

CONTAINER_TARGETS := all size hosttest simtest analyse test misra repro mutate format \
	precommit image image_size image_misra

.PHONY: $(CONTAINER_TARGETS) clean hooks

clean:
	rm -rf $(BUILD)

# Point git at the versioned hooks in .githooks: the commit-msg hook checks the
# subject with the CI script, and the pre-commit hook runs `make precommit`.
hooks:
	git config core.hooksPath .githooks

ifndef PSCU_TOOLCHAIN

$(CONTAINER_TARGETS):
	$(PYTHON) tools/docker_make.py $@ REGION=$(REGION) TIMING=$(TIMING) VCD_FILTER=$(VCD_FILTER)

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

LOGIC_C := src/region.c src/subq.c src/board_mode.c src/inject.c src/led.c src/calib.c src/trim.c
HOST_LOGIC_C := $(LOGIC_C)

FIRMWARE_C := $(LOGIC_C) src/engine.c src/run.c src/main.c
CPPCHECK_MCU_DEF := -D__AVR_ATtiny84__

ALL_SRC_C := $(FIRMWARE_C)
FIRMWARE_S := src/port.S
FIRMWARE_H := $(wildcard include/pscu/*.h) $(wildcard include/port/*.h)
HOST_TEST_C := tests/host/host_assert.c tests/host/host_test.c tests/host/calib_test.c tests/host/trim_test.c tests/host/disc_test.c
SIM_TEST_C := tests/sim/sim_test.c tests/sim/harness.c tests/sim/sim_inject.c tests/sim/sim_disc.c tests/sim/sim_calib.c
SIM_TEST_H := tests/sim/harness.h tests/sim/scenarios.h
C_FILES := $(ALL_SRC_C) $(FIRMWARE_H) $(HOST_TEST_C) $(SIM_TEST_C) $(SIM_TEST_H)
HOST_TEST := $(BUILD)/host/host_test
SIM_TEST := $(BUILD)/sim/sim_test
SIM_CFLAGS := $(C_STD) -O2 -Wall -Wextra -Werror \
	$(patsubst -I%,-isystem %,$(shell pkg-config --cflags simavr libelf))
SIM_LIBS := $(shell pkg-config --libs simavr libelf)

RELEASE := $(BUILD)/$(MCU)$(VARIANT)/release
RELEASE_ELF := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).elf
RELEASE_HEX := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).hex
RELEASE_OBJECTS := $(patsubst src/%,$(RELEASE)/%.o,$(FIRMWARE_C) $(FIRMWARE_S))

AVR_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(REGION_DEF) $(TIMING_DEF) $(VCD_DEF) $(C_STD) -Os -flto -ffat-lto-objects -Iinclude \
	$(WARNINGS) -fno-common -ffunction-sections -fdata-sections
AVR_ASFLAGS := -mmcu=$(MCU) -x assembler-with-cpp -DF_CPU=$(F_CPU) $(TIMING_DEF) -Iinclude -Wall -Wextra -Werror
AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections

AVR_INCLUDE := /usr/lib/avr/include
AVR_GCC_INCLUDE := $(shell $(AVR_CC) -print-file-name=include)
CPPCHECK_FLAGS := --std=c17 --platform=avr8 --enable=all --check-level=exhaustive \
	--error-exitcode=1 --suppress=checkersReport --inline-suppr \
	'--suppress=*:$(AVR_INCLUDE)/*' '--suppress=*:$(AVR_GCC_INCLUDE)/*' \
	-Iinclude -I$(AVR_INCLUDE) -I$(AVR_GCC_INCLUDE) \
	$(CPPCHECK_MCU_DEF) $(REGION_DEF) $(TIMING_DEF) $(VCD_DEF) -DF_CPU=$(F_CPU)
CPPCHECK_CONFIGS := -DPSCU_DEBUG -UPSCU_DEBUG

all: image

image: $(RELEASE_HEX)

# Objects also depend on this Makefile: the build knobs live here as -D flags,
# so changing one must rebuild the objects rather than link stale ones.
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

size: image_size

image_size: $(RELEASE_ELF)
	$(AVR_SIZE) $(RELEASE_ELF)

hosttest: $(HOST_TEST)
	rm -f $(BUILD)/host/*.gcda
	$(HOST_TEST)
	gcovr --root . --filter 'src/' --exclude-branches-by-pattern '.*PSCU_ASSERT.*' \
		--fail-under-line 100 --fail-under-branch 100 --print-summary $(BUILD)/host

$(HOST_TEST): $(HOST_LOGIC_C) $(HOST_TEST_C) $(FIRMWARE_H)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -O0 -DPSCU_DEBUG --coverage -o $@ $(HOST_LOGIC_C) $(HOST_TEST_C)

SIM_ELF := $(BUILD)/attiny84/release/$(NAME)-attiny84.elf
SIM_ELF_VCD := $(BUILD)/attiny84-jp-vcd/release/$(NAME)-attiny84-jp-vcd.elf

$(SIM_TEST): $(SIM_TEST_C) $(SIM_TEST_H) all
	@mkdir -p $(@D)
	$(HOST_CC) $(SIM_CFLAGS) -o $@ $(SIM_TEST_C) $(SIM_LIBS)

# The simulator runs the firmware at the nominal 8 MHz it is built for; the
# oscillator trim scenarios model a fast or slow RC by running the console side
# at a different rate. simavr does not change speed when OSCCAL is written, so
# the trim is checked for direction, bounds and persistence, not its effect. The
# SCPH-5903 Video-CD image is the optional second argument.
simtest: $(SIM_TEST)
	$(MAKE) --no-print-directory REGION=jp VCD_FILTER=on image
	$(SIM_TEST) $(SIM_ELF) $(F_CPU) $(SIM_ELF_VCD)

test: hosttest simtest

format:
	clang-format -i $(C_FILES)

# The quick front of analyse, for the pre-commit hook: formatting and Python
# lint only, no build.
precommit:
	clang-format --dry-run --Werror $(C_FILES)
	ruff check
	ruff format --check

analyse: all
	clang-format --dry-run --Werror $(C_FILES)
	ruff check
	ruff format --check
	reuse lint
	$(MAKE) misra
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage run --branch --source=tools -m unittest discover -s tests -t . -p 'test_*.py'
	COVERAGE_FILE=$(BUILD)/.coverage $(PYTHON) -m coverage report -m

misra: image_misra

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

endif
