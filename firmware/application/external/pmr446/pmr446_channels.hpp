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

#ifndef __PMR446_CHANNELS_H__
#define __PMR446_CHANNELS_H__

/* Analogue PMR446 channel plan (16 x 12.5 kHz NFM, ECC/DEC/(15)05). */

#include <cstddef>
#include <cstdint>

namespace pmr446 {

constexpr size_t analog_channel_count = 16;

/* Analogue PMR446 voice channel n (1..16): 446.00625 MHz + 12.5 kHz. */
constexpr uint32_t analog_frequency_hz(size_t channel) {
    return 446006250 + (channel - 1) * 12500;
}

} /* namespace pmr446 */

#endif /*__PMR446_CHANNELS_H__*/
