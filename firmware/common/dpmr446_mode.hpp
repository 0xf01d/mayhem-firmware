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

#ifndef __DPMR446_MODE_H__
#define __DPMR446_MODE_H__

/* Which direction the combined dPMR446 baseband image should run in.
 *
 * The image holds both processors and one main(), so the direction has to
 * survive the load. It travels in shared memory: the app writes it before
 * baseband::run_image(), the image reads it on entry. A message would not do,
 * because the dispatcher that would deliver it is inside the processor that
 * has not been chosen yet.
 */

#include <cstdint>

enum dpmr446_mode_t : uint8_t {
    dpmr446_mode_rx = 0, /* default: receive, transmits nothing */
    dpmr446_mode_tx = 1,
};

#ifdef __cplusplus
namespace dpmr446 {

/* Both sides include this header, so the slot is declared once, in
 * portapack_shared_memory.hpp; these are just the typed accessors. */
void set_mode(dpmr446_mode_t mode);
dpmr446_mode_t requested_mode();

}  // namespace dpmr446

inline dpmr446_mode_t dpmr446_requested_mode() {
    return dpmr446::requested_mode();
}
#endif

#endif /*__DPMR446_MODE_H__*/
