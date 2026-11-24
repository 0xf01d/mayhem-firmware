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

/* Vocoder state for the combined dPMR446 image.
 *
 * The receive and transmit halves each need their own vocoder state: the
 * AMBE+2 decoder's 5 248 B and the encoder's 28 608 B. In a single-direction
 * image only one of them exists, but the combined image used to link both and
 * pay for both, 33 856 B, which is more than the two banks have left between
 * them.
 *
 * Only one direction ever runs in a given boot: main() picks a direction from
 * shared memory before constructing a processor, and nothing switches it later
 * (the app stops the baseband and loads the image again to change direction).
 * So the two states are never live at the same time and can share one buffer,
 * sized for the larger. That is 28 608 B instead of 33 856 B.
 */

#include "proc_dpmr446_state.hpp"

#include "xombe.h"

#if defined(DPMR446_DUAL_IMAGE)

namespace {

/* Sized for the encoder, which is the larger of the two. The decoder's
 * xombe_dec_init only ever sees the first XOMBE_DEC_SIZE bytes. */
constexpr size_t state_size =
    (XOMBE_ENC_SIZE > XOMBE_DEC_SIZE) ? XOMBE_ENC_SIZE : XOMBE_DEC_SIZE;

}  // namespace

/* In Bank 1 after the code: it is NOLOAD, so nothing has to be copied into it
 * at load time and it does not need a load address in the image. */
__attribute__((aligned(8), section(".vocoder_state"))) uint8_t dpmr446_vocoder_state[state_size];

#endif
