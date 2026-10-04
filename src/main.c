// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "port/port.h"
#include "pscu/run.h"

// Entry point: bring the chip up (clock, ports, watchdog) then hand control to
// the run loop, which never returns. The imperative shell is deliberately this
// small so all testable logic lives in the host-tested layers below it.
int main(void) {
  pscu_port_init();
  pscu_run();
}
