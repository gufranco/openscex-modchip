# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

PYTHON ?= python3
BUILD := build
NAME := openscex-modchip

MCU ?= attiny85
MCUS := attiny85 attiny84
FLASH_BYTES := 8192

CLOCK ?= internal
EXT_F_CPU ?= 4233600UL
ifeq ($(CLOCK),external)
F_CPU := $(EXT_F_CPU)
CLOCK_TAG := -extclk
else
F_CPU := 8000000UL
CLOCK_TAG :=
endif

# Injection bit timing. adaptive (default) locks the WFCK-carrier injection bit
# to the console clock by counting WFCK periods, so the modern-board bit cell is
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

# Boot-ROM BIOS patch model, ATtiny84 only. none (default) links the no-op and
# builds the SCEx-only firmware both chips share; a model name selects the real
# patch and defines its per-BIOS constants. Use it as, e.g.,
# make MCU=attiny84 BIOS=scph_102.
BIOS ?= none
# One-phase models drive a single override; the two oldest Japanese models drive
# a second one on AY, selected by PSCU_BIOS_TWO_PHASE with its own constants.
# PSCU_BIOS_NOISE_TOLERANCE is how many spurious AX highs a quiet window may
# carry and still count as silent, so a brief line glitch does not reset the
# boot-stage detection. It is small against PSCU_BIOS_SILENCE, so a real pulse
# train still breaks a window. Simulation-only, like every BIOS constant.
#
# The per-model values below are Read from PsNee V9.0 settings.h:81-145, the
# ATmega328P column, which PsNee marks as its tested values (kalymos/PsNee
# commit 0d2290a). SCPH-100 and SCPH-102 share one block there. They are
# PsNee's figures for a 16 MHz ATmega that counts the final edge in an
# interrupt; this chip polls at 8 MHz, so the silence counts and cycle offsets
# are not yet derived for it (AGENTS rule 10) and the BIOS builds stay
# experimental until that derivation and a console confirm them.
BIOS_NOISE := -DPSCU_BIOS_NOISE_TOLERANCE=2U
BIOS_ONE := -DPSCU_BIOS_ENABLED=1 -DPSCU_BIOS_TWO_PHASE=0 $(BIOS_NOISE)
BIOS_TWO := -DPSCU_BIOS_ENABLED=1 -DPSCU_BIOS_TWO_PHASE=1 $(BIOS_NOISE)
ifeq ($(BIOS),none)
BIOS_DEF := -DPSCU_BIOS_ENABLED=0 -DPSCU_BIOS_TWO_PHASE=0
BIOS_SRC := src/bios_none.c
BIOS_TAG :=
else ifeq ($(BIOS),scph_102)
BIOS_DEF := $(BIOS_ONE) -DPSCU_BIOS_SILENCE=1500U -DPSCU_BIOS_CONFIRMS=8U -DPSCU_BIOS_PULSES=47U -DPSCU_BIOS_OFFSET_CYCLES=47 -DPSCU_BIOS_OVERRIDE_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_102
else ifeq ($(BIOS),scph_100)
BIOS_DEF := $(BIOS_ONE) -DPSCU_BIOS_SILENCE=1500U -DPSCU_BIOS_CONFIRMS=8U -DPSCU_BIOS_PULSES=47U -DPSCU_BIOS_OFFSET_CYCLES=47 -DPSCU_BIOS_OVERRIDE_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_100
else ifeq ($(BIOS),scph_7000_9000)
BIOS_DEF := $(BIOS_ONE) -DPSCU_BIOS_SILENCE=1500U -DPSCU_BIOS_CONFIRMS=1U -DPSCU_BIOS_PULSES=15U -DPSCU_BIOS_OFFSET_CYCLES=47 -DPSCU_BIOS_OVERRIDE_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_7000_9000
else ifeq ($(BIOS),scph_3500_5500)
BIOS_DEF := $(BIOS_ONE) -DPSCU_BIOS_SILENCE=32000U -DPSCU_BIOS_CONFIRMS=1U -DPSCU_BIOS_PULSES=84U -DPSCU_BIOS_OFFSET_CYCLES=47 -DPSCU_BIOS_OVERRIDE_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_3500_5500
else ifeq ($(BIOS),scph_1000)
BIOS_DEF := $(BIOS_TWO) -DPSCU_BIOS_SILENCE=1500U -DPSCU_BIOS_CONFIRMS=9U -DPSCU_BIOS_PULSES=91U -DPSCU_BIOS_OFFSET_CYCLES=45 -DPSCU_BIOS_OVERRIDE_CYCLES=3 -DPSCU_BIOS_CONFIRMS_2=222U -DPSCU_BIOS_PULSES_2=70U -DPSCU_BIOS_OFFSET_2_CYCLES=48 -DPSCU_BIOS_OVERRIDE_2_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_1000
else ifeq ($(BIOS),scph_3000)
BIOS_DEF := $(BIOS_TWO) -DPSCU_BIOS_SILENCE=1500U -DPSCU_BIOS_CONFIRMS=9U -DPSCU_BIOS_PULSES=59U -DPSCU_BIOS_OFFSET_CYCLES=45 -DPSCU_BIOS_OVERRIDE_CYCLES=3 -DPSCU_BIOS_CONFIRMS_2=206U -DPSCU_BIOS_PULSES_2=42U -DPSCU_BIOS_OFFSET_2_CYCLES=48 -DPSCU_BIOS_OVERRIDE_2_CYCLES=3
BIOS_SRC := src/bios.c
BIOS_TAG := -scph_3000
else
$(error unknown BIOS model '$(BIOS)'; use none, scph_102, scph_100, scph_7000_9000, scph_3500_5500, scph_1000 or scph_3000)
endif

# The BIOS patch needs the ATtiny84's spare pins, so refuse it on any other chip
# with a clear message rather than a cryptic assembler error about the missing
# pin macros.
ifneq ($(BIOS),none)
ifneq ($(MCU),attiny84)
$(error BIOS=$(BIOS) needs MCU=attiny84; the ATtiny85 has no pins for the boot-ROM patch)
endif
endif

# Each clock, region, BIOS, timing and filter build differs in generated code,
# so their objects must never share a directory; VARIANT keeps them separate.
# An empty VARIANT (internal-clock America, no patch) keeps the plain artifact
# name the sim uses.
VARIANT := $(CLOCK_TAG)$(REGION_TAG)$(BIOS_TAG)$(TIMING_TAG)$(VCD_TAG)

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
	$(PYTHON) tools/docker_make.py $@ MCU=$(MCU) CLOCK=$(CLOCK) EXT_F_CPU=$(EXT_F_CPU) REGION=$(REGION) BIOS=$(BIOS) TIMING=$(TIMING) VCD_FILTER=$(VCD_FILTER)

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

LOGIC_C := src/region.c src/subq.c src/board_mode.c src/inject.c src/diag.c
HOST_LOGIC_C := $(LOGIC_C)

# The SCEx stealth firmware is shared by both chips; BIOS_SRC adds the patch
# (bios.c) or the no-op (bios_none.c).
SCEX_C := $(LOGIC_C) src/engine.c src/run.c src/main.c
FIRMWARE_C := $(SCEX_C) $(BIOS_SRC)
ifeq ($(MCU),attiny84)
CPPCHECK_MCU_DEF := -D__AVR_ATtiny84__
else
CPPCHECK_MCU_DEF := -D__AVR_ATtiny85__
endif

ALL_SRC_C := $(SCEX_C) src/bios.c src/bios_none.c
FIRMWARE_S := src/port.S
FIRMWARE_H := $(wildcard include/pscu/*.h) $(wildcard include/port/*.h)
HOST_TEST_C := tests/host/host_assert.c tests/host/host_test.c
SIM_TEST_C := tests/sim/sim_test.c
C_FILES := $(ALL_SRC_C) $(FIRMWARE_H) $(HOST_TEST_C) $(SIM_TEST_C)
HOST_TEST := $(BUILD)/host/host_test
SIM_TEST := $(BUILD)/sim/sim_test
SIM_CFLAGS := $(C_STD) -O2 -Wall -Wextra -Werror \
	$(patsubst -I%,-isystem %,$(shell pkg-config --cflags simavr libelf))
SIM_LIBS := $(shell pkg-config --libs simavr libelf)
SIM_CLOCKS_HZ := 7200000 8000000 8800000

RELEASE := $(BUILD)/$(MCU)$(VARIANT)/release
RELEASE_ELF := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).elf
RELEASE_HEX := $(RELEASE)/$(NAME)-$(MCU)$(VARIANT).hex
RELEASE_OBJECTS := $(patsubst src/%,$(RELEASE)/%.o,$(FIRMWARE_C) $(FIRMWARE_S))

AVR_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(REGION_DEF) $(BIOS_DEF) $(TIMING_DEF) $(VCD_DEF) $(C_STD) -Os -flto -ffat-lto-objects -Iinclude \
	$(WARNINGS) -fno-common -ffunction-sections -fdata-sections
AVR_ASFLAGS := -mmcu=$(MCU) -x assembler-with-cpp -DF_CPU=$(F_CPU) $(BIOS_DEF) $(TIMING_DEF) -Iinclude -Wall -Wextra -Werror
AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections

AVR_INCLUDE := /usr/lib/avr/include
AVR_GCC_INCLUDE := $(shell $(AVR_CC) -print-file-name=include)
CPPCHECK_FLAGS := --std=c17 --platform=avr8 --enable=all --check-level=exhaustive \
	--error-exitcode=1 --suppress=checkersReport --inline-suppr \
	'--suppress=*:$(AVR_INCLUDE)/*' '--suppress=*:$(AVR_GCC_INCLUDE)/*' \
	-Iinclude -I$(AVR_INCLUDE) -I$(AVR_GCC_INCLUDE) \
	$(CPPCHECK_MCU_DEF) $(REGION_DEF) $(BIOS_DEF) $(TIMING_DEF) $(VCD_DEF) -DF_CPU=$(F_CPU)
CPPCHECK_CONFIGS := -DPSCU_DEBUG -UPSCU_DEBUG

all:
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) image &&) true

image: $(RELEASE_HEX)

# Objects also depend on this Makefile: the per-model BIOS constants and the
# build knobs live here as -D flags, so changing one must rebuild the objects
# rather than link stale ones.
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

size:
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) image_size &&) true

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

SIM_ELF85 := $(BUILD)/attiny85/release/$(NAME)-attiny85.elf
SIM_ELF84 := $(BUILD)/attiny84/release/$(NAME)-attiny84.elf
SIM_ELF84_BIOS := $(BUILD)/attiny84-scph_102/release/$(NAME)-attiny84-scph_102.elf
SIM_ELF84_BIOS2 := $(BUILD)/attiny84-scph_1000/release/$(NAME)-attiny84-scph_1000.elf
SIM_ELF85_VCD := $(BUILD)/attiny85-jp-vcd/release/$(NAME)-attiny85-jp-vcd.elf

$(SIM_TEST): $(SIM_TEST_C) all
	@mkdir -p $(@D)
	$(HOST_CC) $(SIM_CFLAGS) -o $@ $(SIM_TEST_C) $(SIM_LIBS)

# Build one single-phase and one two-phase ATtiny84 BIOS image and the ATtiny85
# SCPH-5903 Video-CD image, and hand them to the 8 MHz run as the fourth, fifth
# and sixth arguments; the SCEx scenarios run on both base images at every clock.
simtest: $(SIM_TEST)
	$(MAKE) --no-print-directory MCU=attiny84 BIOS=scph_102 image
	$(MAKE) --no-print-directory MCU=attiny84 BIOS=scph_1000 image
	$(MAKE) --no-print-directory MCU=attiny85 REGION=jp VCD_FILTER=on image
	$(SIM_TEST) $(SIM_ELF85) $(SIM_ELF84) 7200000
	$(SIM_TEST) $(SIM_ELF85) $(SIM_ELF84) 8000000 $(SIM_ELF84_BIOS) $(SIM_ELF84_BIOS2) $(SIM_ELF85_VCD)
	$(SIM_TEST) $(SIM_ELF85) $(SIM_ELF84) 8800000

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

misra:
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) image_misra &&) true

image_misra:
	$(foreach config,$(CPPCHECK_CONFIGS),cppcheck $(CPPCHECK_FLAGS) $(config) --addon=misra $(FIRMWARE_C) &&) true

REPRO_A := $(BUILD)/repro-a
REPRO_B := $(BUILD)/repro-b

repro:
	rm -rf $(REPRO_A) $(REPRO_B)
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) BUILD=$(REPRO_A) image &&) true
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) BUILD=$(REPRO_B) image &&) true
	cd $(REPRO_A) && find . -type f \( -name '*.hex' -o -name '*.elf' \) | sort | xargs sha256sum > $(CURDIR)/$(BUILD)/repro-a.sums
	cd $(REPRO_B) && find . -type f \( -name '*.hex' -o -name '*.elf' \) | sort | xargs sha256sum > $(CURDIR)/$(BUILD)/repro-b.sums
	diff $(BUILD)/repro-a.sums $(BUILD)/repro-b.sums
	@echo "reproducible build verified: identical artifacts across two fresh builds"

mutate:
	$(PYTHON) tools/mutate.py

endif
