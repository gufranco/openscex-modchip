// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "port/port.h"
#include "pscu/run.h"

int main(void) {
  pscu_port_init();
  pscu_run();
}
