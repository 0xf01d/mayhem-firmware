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

/* dPMR446 4FSK modulation constants (TS 102 490 12.2), shared by the
 * baseband transmit processor and the host unit tests.
 *
 * The modulator (12.2.2.3) is a square root raised cosine filter, r = 0.2,
 * T = 1/2400, cascaded with a frequency modulator. The SRRC shapes the
 * symbol levels into a frequency-control waveform whose value is the
 * instantaneous deviation in units of 350 Hz. */

#ifndef __DPMR_MOD_H__
#define __DPMR_MOD_H__

#include <cstddef>
#include <cstdint>

namespace dpmr_mod {

constexpr uint32_t symbol_rate = 2400;   /* 12.2.1 */
constexpr float deviation_unit_hz = 350.0f;  /* per level unit */
constexpr float max_deviation_hz = 3.0f * deviation_unit_hz; /* 1050 Hz */

/* Di-bit levels (table 12.1): 01 -> +3, 00 -> +1, 10 -> -1, 11 -> -3. */
inline int8_t dibit_deviation_units(uint8_t dibit) {
    static constexpr int8_t level[4] = {3, 1, -1, -3};
    return level[dibit & 3u];
}

/* SRRC taps at `sps` samples per symbol, spanning +-6 symbols (121 taps),
 * r = 0.2, normalized to unit DC gain so a constant symbol stream produces
 * exactly its level in units. Generated from the 12.2.2.2 response
 * H(f); values are precomputed so target and host agree bit for bit. */
constexpr size_t rrc_span_symbols = 6;
constexpr size_t rrc_taps_per_symbol = 10; /* shaping rate 24 kHz */
constexpr size_t rrc_tap_count = 2 * rrc_span_symbols * rrc_taps_per_symbol + 1;

constexpr float rrc_taps[rrc_tap_count] = {
    +0.000791185f,
    +0.000961280f,
    +0.000986160f,
    +0.000848040f,
    +0.000552245f,
    +0.000128470f,
    -0.000371346f,
    -0.000878499f,
    -0.001315874f,
    -0.001608240f,
    -0.001693241f,
    -0.001531610f,
    -0.001115147f,
    -0.000471176f,
    +0.000337462f,
    +0.001217264f,
    +0.002053569f,
    +0.002723889f,
    +0.003113633f,
    +0.003132392f,
    +0.002728702f,
    +0.001901219f,
    +0.000704519f,
    -0.000751735f,
    -0.002310778f,
    -0.003783579f,
    -0.004969292f,
    -0.005679343f,
    -0.005762493f,
    -0.005127924f,
    -0.003763321f,
    -0.001745309f,
    +0.000759745f,
    +0.003505576f,
    +0.006186363f,
    +0.008466207f,
    +0.010015195f,
    +0.010548596f,
    +0.009865050f,
    +0.007879463f,
    +0.004646536f,
    +0.000371609f,
    -0.004593392f,
    -0.009769641f,
    -0.014585893f,
    -0.018425311f,
    -0.020680722f,
    -0.020813357f,
    -0.018409420f,
    -0.013228835f,
    -0.005240965f,
    +0.005356816f,
    +0.018140292f,
    +0.032480102f,
    +0.047582756f,
    +0.062547860f,
    +0.076436810f,
    +0.088346958f,
    +0.097484645f,
    +0.103230465f,
    +0.105190739f,
    +0.103230465f,
    +0.097484645f,
    +0.088346958f,
    +0.076436810f,
    +0.062547860f,
    +0.047582756f,
    +0.032480102f,
    +0.018140292f,
    +0.005356816f,
    -0.005240965f,
    -0.013228835f,
    -0.018409420f,
    -0.020813357f,
    -0.020680722f,
    -0.018425311f,
    -0.014585893f,
    -0.009769641f,
    -0.004593392f,
    +0.000371609f,
    +0.004646536f,
    +0.007879463f,
    +0.009865050f,
    +0.010548596f,
    +0.010015195f,
    +0.008466207f,
    +0.006186363f,
    +0.003505576f,
    +0.000759745f,
    -0.001745309f,
    -0.003763321f,
    -0.005127924f,
    -0.005762493f,
    -0.005679343f,
    -0.004969292f,
    -0.003783579f,
    -0.002310778f,
    -0.000751735f,
    +0.000704519f,
    +0.001901219f,
    +0.002728702f,
    +0.003132392f,
    +0.003113633f,
    +0.002723889f,
    +0.002053569f,
    +0.001217264f,
    +0.000337462f,
    -0.000471176f,
    -0.001115147f,
    -0.001531610f,
    -0.001693241f,
    -0.001608240f,
    -0.001315874f,
    -0.000878499f,
    -0.000371346f,
    +0.000128470f,
    +0.000552245f,
    +0.000848040f,
    +0.000986160f,
    +0.000961280f,
    +0.000791185f,
};

} /* namespace dpmr_mod */

#endif /*__DPMR_MOD_H__*/
