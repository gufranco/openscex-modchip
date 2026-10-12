// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "port/port.h"
#include "pscu/run.h"

// Bring up the clock, ports and watchdog, then run; pscu_run never returns.
int main(void) {
  pscu_port_init();
  pscu_run();
}
