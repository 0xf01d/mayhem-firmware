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

#ifndef __DPMR446_CHANNELS_H__
#define __DPMR446_CHANNELS_H__

/* Channel plans for dPMR446 (32 x 6.25 kHz 4FSK, tables 6.1/6.2 of
 * ETSI TS 102 490), carried in the shared dpmr.hpp codec. */

#include "dpmr.hpp"

namespace dpmr446 {

using dpmr::channel_code_24;
using dpmr::channel_code_decode;
using dpmr::channel_count;
using dpmr::channel_frequency_hz;
using dpmr::isf_common_id;

} /* namespace dpmr446 */

#endif /*__DPMR446_CHANNELS_H__*/
