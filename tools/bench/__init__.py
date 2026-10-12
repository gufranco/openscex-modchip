# SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
# SPDX-License-Identifier: MIT

"""Console bench: one console model driving every modchip firmware.

Each scenario's signal timeline is played into a firmware image under simavr or
gpsim, and DATA and the gate are decoded the same way for every chip. Verdicts
hold within the model, not on a real mechacon.
"""
