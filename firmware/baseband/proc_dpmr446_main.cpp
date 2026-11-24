/*
 * Copyright (C) 2026 Mayhem fork contributors
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/* Single entry point for the combined dPMR446 image.
 *
 * The receiver and the transmitter keep their own processors, but an M4 image
 * has exactly one main(), so this picks one before handing over. The
 * direction comes from the app in the DPMR446ModeMessage it queues just
 * before baseband::run_image(); the queue survives the load, which is what
 * lets one image serve both directions.
 */

#include "dpmr446_mode.hpp"
#include "portapack_shared_memory.hpp"

int dpmr446_voice_entry();
int dpmr446_tx_entry();

int main() {
    /* The app has already put the mode in shared memory. Anything else (a
     * stale byte after a reset, a bare load from the debugger) starts in
     * receive, which is the safe direction: it transmits nothing. */
    if (dpmr446_requested_mode() == dpmr446_mode_tx)
        return dpmr446_tx_entry();

    return dpmr446_voice_entry();
}
