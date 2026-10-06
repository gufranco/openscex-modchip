# Region unlock behavior spec

The firmware observes the CD subsystem, decides when the console is reading the region-check area, and injects the SCEx string for the configured region, defeating the region lockout. Each requirement states one behavior and carries at least one Given/When/Then scenario; each scenario maps to a check in the host suite (`tests/host/host_test.c`) or the simavr console model (`tests/sim/sim_test.c`). Compatibility claims stay Unknown until a console confirms them.

## SCEx encoding

### Requirement: the firmware MUST emit each region string as 44 bits, least significant bit first
#### Scenario: NTSC-J string
- GIVEN the target region is NTSC-J
- WHEN the 44 injection bits are read in order
- THEN they equal 10011010100100111101001010111010010110110100

#### Scenario: region differs only in the fifth byte
- GIVEN the target region is NTSC-U/C or PAL
- WHEN the 44 injection bits are read
- THEN bits 0 to 31 and bits 40 to 43 match NTSC-J, and bits 32 to 39 select the region

### Requirement: a build MUST emit only its one configured region string
#### Scenario: America build
- GIVEN the default build
- WHEN an injection runs on any board family
- THEN the decoded word is SCEA and never another region's string

## SUBQ capture

### Requirement: each SUBQ capture MUST start on the first bit of a frame
#### Scenario: capture begins inside a burst
- GIVEN the console is already part-way through a frame when the firmware starts listening
- WHEN the firmware captures frames
- THEN it waits for SQCK to stay idle-high for at least 1 ms, realigns on the next frame, and injects once enough region-check frames follow

### Requirement: a capture that times out MUST be treated as a miss
#### Scenario: failed capture
- GIVEN SQCK stops mid-frame or never idles
- WHEN the capture gives up
- THEN the frame is filled with 0xFF, the request counter decays, and the frame is never read as the program area

## SUBQ region-check detection

### Requirement: a sector control byte MUST be classified as data only when bit 6 is set and bits 7 and 4 are clear
#### Scenario: data and non-data control bytes
- GIVEN a control byte
- WHEN it is masked with 0xD0
- THEN the result equals 0x40 for a data sector and differs otherwise

### Requirement: the request counter MUST rise only on a framed lead-in sample that matches a region-check pattern, and decay otherwise
#### Scenario: lead-in table of contents
- GIVEN a framed data sector (TNO 0x00, ZERO byte 0x00) whose POINT is at or above 0xA0
- WHEN the counter is updated
- THEN the counter increases by one

#### Scenario: point 01 at the end of the lead-in
- GIVEN a framed data sector with POINT 0x01 and a running minute of 98, 99, 00, 01 or 02 (BCD)
- WHEN the counter is updated
- THEN the counter increases by one, and a minute of 97 or 03 does not raise it

#### Scenario: armed tracking inside the lead-in
- GIVEN the counter is already above zero and a framed lead-in sample has an audio (0x01) or data control byte
- WHEN the counter is updated
- THEN the counter increases by one

#### Scenario: program-area, unframed, or non-matching sample
- GIVEN a sample whose TNO or ZERO byte is nonzero, or a lead-in sample outside every pattern with the counter at zero
- WHEN the counter is updated
- THEN the counter decays by one toward zero and never below zero

### Requirement: the request counter MUST NOT wrap
#### Scenario: counter at maximum
- GIVEN the counter is at its maximum
- WHEN a matching sample updates it
- THEN the counter stays at the maximum

## Board-generation detection

### Requirement: the firmware MUST classify a static-high WFCK as the legacy gate and an oscillating WFCK as the modern clock
#### Scenario: static high
- GIVEN a window of WFCK samples that never goes low
- WHEN the board mode is decided
- THEN the mode is legacy gate

#### Scenario: oscillating
- GIVEN a window of WFCK samples with at least the required number of low pulses
- WHEN the board mode is decided
- THEN the mode is WFCK sync

### Requirement: board detection MUST wait for WFCK to settle before sampling
#### Scenario: carrier starts after power-up
- GIVEN a carrier board whose WFCK starts oscillating 150 ms after the chip powers up
- WHEN the board is detected
- THEN it is detected as a carrier board and injection mirrors the carrier

## Injection and stealth

### Requirement: injection MUST fire only while the request counter is at or above the trigger, and at most the stealth cap of strings per arming
#### Scenario: at and below the trigger
- GIVEN the request counter
- WHEN it is compared to the trigger
- THEN the window is open at or above the trigger and closed below it

#### Scenario: cap reached
- GIVEN the window stays open after the cap of strings has been emitted
- WHEN further frames arrive
- THEN no further string is emitted until the window closes

### Requirement: leaving the window MUST re-arm injection
#### Scenario: disc swap
- GIVEN an arming has emitted strings and the console moves to the program area
- WHEN the window closes and a new disc's lead-in reopens it
- THEN injection fires again for the new disc

### Requirement: a stalled WFCK carrier MUST NOT leave DATA driven
#### Scenario: carrier stops mid-injection
- GIVEN an injection is running on a carrier board
- WHEN WFCK stops oscillating
- THEN the watchdog resets the chip within about 0.5 s and DATA returns to high-Z

## Diagnostics and confirmation

### Requirement: each arming MUST be recorded as its own session
#### Scenario: two discs in one power cycle
- GIVEN two discs are checked in one power cycle
- WHEN the flight recorder is read
- THEN it holds two sessions and the second disc's outcome

### Requirement: a session MUST be confirmed only when a program-area frame follows the injection
#### Scenario: program area reached
- GIVEN an injection has run
- WHEN a frame with a BCD track number 01..99 in TNO arrives
- THEN the session is recorded as confirmed

#### Scenario: no program area
- GIVEN an injection has run
- WHEN only lead-in or silent frames follow until the wait expires
- THEN the session is recorded as unconfirmed

## Boot-ROM BIOS patch (ATtiny84, experimental)

### Requirement: each override MUST fire on the final counted edge of its pulse train
#### Scenario: one-phase model
- GIVEN the boot-stage silent windows have been counted
- WHEN the model's AX pulse train arrives
- THEN DX is driven low right after the last rising edge and released

#### Scenario: two-phase model
- GIVEN a two-phase model
- WHEN the first AX train and then the AY train arrive
- THEN DX is driven high, then low, after the last AX rising edge, and low after the last AY falling edge

### Requirement: the BIOS patch MUST NOT prevent SCEx injection
#### Scenario: AX never toggles
- GIVEN the AX line is dead or miswired
- WHEN the patch waits for its first edge
- THEN it gives up after its bounded wait and SCEx injection still runs

#### Scenario: AX stops mid-train
- GIVEN the AX pulses stop part-way through the counted train
- WHEN the watchdog expires
- THEN the chip reboots, skips the patch because the reset came from the watchdog, and SCEx injection still runs

## Not yet specified

Register-level timing of the BIOS override against the console bus, and every hardware claim, stay Unknown until a console confirms them. The BIOS constants are PsNee's 16 MHz ATmega figures and are not yet derived for this 8 MHz polled chip.
