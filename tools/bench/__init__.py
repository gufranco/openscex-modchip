# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Console bench: one console model driving every modchip firmware.

A scenario is a timeline of the signals a console puts on the wires a modchip
reads. A runner per simulator (simavr for AVR, gpsim for PIC) plays the
timeline into one firmware image and records what the chip puts on DATA and
the gate. The scorer decodes those traces the same way for every chip, so ours
can be measured against chips real consoles have accepted for years. Nothing
here proves what a real mechacon accepts; every verdict is "within the model".
"""
