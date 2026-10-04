# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

PYTHON ?= python3
BUILD := build
NAME := psone-cdr-unlock

MCU ?= attiny85
MCUS := attiny85 attiny84
F_CPU := 8000000UL
FLASH_BYTES := 8192

CONTAINER_TARGETS := all size hosttest simtest analyse test misra

.PHONY: $(CONTAINER_TARGETS) clean

clean:
	rm -rf $(BUILD)

ifndef PSCU_TOOLCHAIN

$(CONTAINER_TARGETS):
	$(PYTHON) tools/docker_make.py $@

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

LOGIC_C := src/region.c src/subq.c src/board_mode.c src/inject.c
HOST_LOGIC_C := $(LOGIC_C) src/mode.c

ifeq ($(MCU),attiny85)
FIRMWARE_C := $(LOGIC_C) src/engine.c src/run_basic.c src/main.c
CPPCHECK_MCU_DEF := -D__AVR_ATtiny85__
else
FIRMWARE_C := $(LOGIC_C) src/engine.c src/run_modes.c src/mode.c src/main.c
CPPCHECK_MCU_DEF := -D__AVR_ATtiny84__
endif

ALL_SRC_C := $(LOGIC_C) src/mode.c src/engine.c src/run_basic.c src/run_modes.c src/main.c
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

RELEASE := $(BUILD)/$(MCU)/release
RELEASE_ELF := $(RELEASE)/$(NAME)-$(MCU).elf
RELEASE_HEX := $(RELEASE)/$(NAME)-$(MCU).hex
RELEASE_OBJECTS := $(patsubst src/%,$(RELEASE)/%.o,$(FIRMWARE_C) $(FIRMWARE_S))

AVR_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(C_STD) -Os -flto -ffat-lto-objects -Iinclude \
	$(WARNINGS) -fno-common -ffunction-sections -fdata-sections
AVR_ASFLAGS := -mmcu=$(MCU) -x assembler-with-cpp -DF_CPU=$(F_CPU) -Iinclude -Wall -Wextra -Werror
AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections

AVR_INCLUDE := /usr/lib/avr/include
AVR_GCC_INCLUDE := $(shell $(AVR_CC) -print-file-name=include)
CPPCHECK_FLAGS := --std=c17 --platform=avr8 --enable=all --check-level=exhaustive \
	--error-exitcode=1 --suppress=checkersReport --inline-suppr \
	'--suppress=*:$(AVR_INCLUDE)/*' '--suppress=*:$(AVR_GCC_INCLUDE)/*' \
	-Iinclude -I$(AVR_INCLUDE) -I$(AVR_GCC_INCLUDE) \
	$(CPPCHECK_MCU_DEF) -DF_CPU=$(F_CPU)
CPPCHECK_CONFIGS := -DPSCU_DEBUG -UPSCU_DEBUG

all:
	$(foreach mcu,$(MCUS),$(MAKE) --no-print-directory MCU=$(mcu) image &&) true

image: $(RELEASE_HEX)

$(RELEASE)/%.c.o: src/%.c $(FIRMWARE_H)
	@mkdir -p $(@D)
	$(AVR_CC) $(AVR_CFLAGS) -c -o $@ $<

$(RELEASE)/%.S.o: src/%.S include/port/registers.h
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

$(SIM_TEST): $(SIM_TEST_C) all
	@mkdir -p $(@D)
	$(HOST_CC) $(SIM_CFLAGS) -o $@ $(SIM_TEST_C) $(SIM_LIBS)

simtest: $(SIM_TEST)
	$(foreach clk,$(SIM_CLOCKS_HZ),$(SIM_TEST) $(SIM_ELF85) $(SIM_ELF84) $(clk) &&) true

test: hosttest simtest

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

endif
