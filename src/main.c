// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "port/port.h"
#include "pscu/run.h"

// Bring up the clock, ports and watchdog, then run; pscu_run never returns.
// The return is never reached; MISRA C:2012 rule 17.4 still wants every
// non-void function to end in one.
int main(void) {
  pscu_port_init();
  pscu_run();
  return 0;
}
