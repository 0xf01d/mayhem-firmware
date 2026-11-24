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

#ifndef __PROC_DPMR444_H__
#define __PROC_DPMR444_H__

/* dPMR446 receiver (ETSI TS 102 490 V1.9.1):
 * 4FSK 2400 baud (12.2), sync/frame structure (6.3-6.5, 8, 9),
 * layer-2 decoding through the FEC chains of clause 7/9. */

#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "dsp_decimate.hpp"
#include "dsp_demodulate.hpp"
#include "dsp_fir_taps.hpp"
#include "dpmr.hpp"
#include "message.hpp"
#include "rssi_thread.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace dpmr;

class DPMRProcessor : public BasebandProcessor {
   public:
    DPMRProcessor();

    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

   private:
    static constexpr size_t baseband_fs = 3072000;
    static constexpr size_t channel_fs = 48000;
    static constexpr uint32_t symbol_rate = 2400; /* 12.2.1 */

    /* Sync words (6.3/6.4), leftmost bit transmitted first. */
    static constexpr uint64_t fs1 = 0x57FF5F75D577ULL;
    static constexpr uint32_t fs2 = 0x5FF77D;
    static constexpr uint32_t fs3 = 0x7DDFF5;
    static constexpr uint64_t fs4 = 0xFD55F5DF7FDDULL;

    /* Polarity variants of a sync word, applied to every dibit. */
    static constexpr uint64_t dibit_flip_mask48 = 0xAAAAAAAAAAAAULL;
    static constexpr uint32_t dibit_flip_mask24 = 0xAAAAAAAAUL;

    static constexpr size_t cch_frame_bits = 72;   /* CCH, 9.1 */
    static constexpr size_t tch_frame_bits = 288;  /* 4 x 72, 9.1/9.2/9.3 */
    static constexpr size_t hi_block_bits = 120;   /* HI0/HI1, 9.5 */
    static constexpr size_t cc_bits = 24;          /* colour code */
    static constexpr size_t end_frame_bits = 72;   /* 2 x 36, 9.6 */

    static constexpr uint8_t sync_err_max_24 = 2;
    static constexpr uint8_t sync_err_max_48 = 5;
    static constexpr uint8_t cch_fail_limit = 8;

    enum class RxState : uint8_t {
        Idle,    /* sync word search */
        Header,  /* FS1/FS4: HI0, CC, HI1 */
        SFSync,  /* 24-bit frame prefix: FS2 / CC / FS3 */
        CCH,     /* 72-bit coded control channel */
        TCH,     /* 288-bit coded payload */
        End,     /* 72-bit end frame */
    };

    std::array<complex16_t, 512> dst_0{};
    const buffer_c16_t dst_buffer_0{dst_0.data(), dst_0.size()};

    std::array<complex16_t, 512> dst_1{};
    const buffer_c16_t dst_buffer_1{dst_1.data(), dst_1.size()};

    std::array<float, 64> audio{};
    const buffer_f32_t audio_buffer{audio.data(), audio.size()};

    dsp::decimate::FIRC8xR16x24FS4Decim8 decim_0{};
    dsp::decimate::FIRC16xR16x32Decim8 decim_1{};
    dsp::decimate::FIRAndDecimateComplex channel_filter{};
    dsp::demodulate::FM demod{};

    bool configured{false};

    /* Best-matching polarity variant of a sync word in the (raw) sync
     * register. Returns the popcount error, or -1 above the limit; sets
     * flip_out to the dibit XOR mask that normalises the stream. */
    static int match_sync(
        uint64_t reg,
        uint64_t word,
        size_t word_bits,
        uint8_t err_limit,
        uint8_t& flip_out);

    /* Symbol clock: 20 samples/symbol at 48 kHz, drift-corrected by a
     * decision-directed early/late nudge on level transitions. */
    uint32_t symbol_phase{0};
    static constexpr uint32_t symbol_phase_inc =
        (uint32_t)(((uint64_t)symbol_rate << 32) / channel_fs);

    float sym_sum{0.0f};
    uint32_t sym_count{0};

    /* 4-level slicer normalisation (table 12.1 levels are +/-1 and +/-3).
     * min/max of the symbol decisions approximate the +/-3 levels. */
    float level_min{-1.0f};
    float level_max{1.0f};
    uint8_t last_q{0};
    bool last_q_valid{false};

    /* Sync / frame engine */
    RxState state{RxState::Idle};
    uint64_t sync_register{0};
    uint8_t bit_flip{0}; /* polarity correction XORed into each dibit */
    uint8_t frame_bits[tch_frame_bits]{};
    uint16_t collect_count{0};
    uint16_t collect_target{0};
    uint8_t header_phase{0}; /* Header state: 0=HI0, 1=CC, 2=HI1 */
    uint8_t hi0_decoded[hi_bits]{};
    DecodeResult hi0_result{};
    bool hi0_valid{false};
    uint8_t cch_fail_count{0};
    uint16_t colour_code{0};
    uint32_t called_id{0};
    uint32_t own_id{0};
    uint8_t last_cch_fn{0};

    void on_symbol(float value);
    void process_dibit(uint8_t raw_dibit);
    void handle_sync_search();
    void collect_bit(uint8_t bit);
    void start_collect(size_t bits);
    void handle_block_complete();
    void handle_header_complete();
    void handle_sfsync_complete();
    void handle_cch_complete();
    void handle_tch_complete();
    void handle_end_complete();
    void reset_frame_engine();

    BasebandThread baseband_thread{baseband_fs, this, baseband::Direction::Receive, false};
    RSSIThread rssi_thread{};
};

#endif /*__PROC_DPMR444_H__*/
