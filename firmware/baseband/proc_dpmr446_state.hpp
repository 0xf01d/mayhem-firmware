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

#ifndef __DPMR446_STATE_H__
#define __DPMR446_STATE_H__

/* The vocoder state buffer the combined dPMR446 image shares between its
 * receive and transmit halves; see proc_dpmr446_state.cpp for why they can
 * share it. Single-direction images keep their own buffers and do not use
 * this.
 */

#include <cstddef>
#include <cstdint>

#if defined(DPMR446_DUAL_IMAGE)

extern uint8_t dpmr446_vocoder_state[];

#endif

#endif /*__DPMR446_STATE_H__*/
