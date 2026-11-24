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

#ifndef __PROC_DPMR_RX_H__
#define __PROC_DPMR_RX_H__

/* dPMR446 receive frame engine (ETSI TS 102 490): FM demod output ->
 * symbol clock -> 4-level slicer -> sync search -> layer-2 state machine.
 * Shared by the signalling receiver (proc_dpmr444) and the voice receiver
 * (proc_dpmr446_voice); subclasses implement the TCH payload hooks.
 *
 * TCH disambiguation (9.1 vs 9.2/9.3): a voice superframe scrambles its CCH
 * alone and sends the vocoder bits raw, while type 1/2 data superframes
 * scramble CCH+TCH as one 360-bit run. The CCH is therefore decoded two
 * ways: on a fresh-scramble failure the data hypothesis is tested over the
 * combined block after the TCH has been collected. */

#include "baseband_processor.hpp"
#include "dpmr.hpp"
#include "message.hpp"
#include "portapack_shared_memory.hpp"

#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>

using namespace dpmr;

class DPMRRxEngine {
   public:
    static constexpr uint32_t symbol_rate = 2400; /* 12.2.1 */
    static constexpr uint32_t channel_fs = 48000;

    void on_symbol(float value) {
        sym_sum += value;
        sym_count++;

        const uint32_t old_phase = symbol_phase;
        symbol_phase += symbol_phase_inc;
        if (symbol_phase >= old_phase)
            return;

        /* Symbol boundary: matched-window mean of the FM demod output. */
        const float m = sym_sum / (sym_count ? static_cast<float>(sym_count) : 1.0f);
        sym_sum = 0.0f;
        sym_count = 0;

        /* Track the +/-3 levels (table 12.1) for the slicer normalisation.
         * Slow decay keeps outliers from wedging the range. */
        level_min *= 0.9998f;
        level_max *= 0.9998f;
        if (m < level_min) level_min = m;
        if (m > level_max) level_max = m;

        const float c = (level_max + level_min) * 0.5f;
        const float u = (level_max - level_min) / 6.0f;
        if (u < 0.02f)
            return; /* no usable signal amplitude yet */

        uint8_t q; /* 0..3 for levels -3,-1,+1,+3 */
        if (m >= c + 2.0f * u)
            q = 3;
        else if (m >= c)
            q = 2;
        else if (m >= c - 2.0f * u)
            q = 1;
        else
            q = 0;

        /* Decision-directed early/late: on a level transition, if the window
         * mean sits closer to the previous symbol's level, the window still
         * holds previous-symbol energy (early); otherwise it is late. */
        const float level_of_q[4] = {c - 3.0f * u, c - u, c + u, c + 3.0f * u};
        if (last_q_valid && q != last_q) {
            if (fabsf(m - level_of_q[q]) > fabsf(m - level_of_q[last_q]))
                symbol_phase -= symbol_phase_inc / 8; /* early */
            else
                symbol_phase += symbol_phase_inc / 8; /* late */
        }
        last_q = q;
        last_q_valid = true;

        /* Dibit mapping, table 12.1: 01=+1050, 00=+350, 10=-350, 11=-1050. */
        const uint8_t dibit = (q == 3) ? 0b01 : (q == 2) ? 0b00 : (q == 1) ? 0b10 : 0b11;
        process_dibit(dibit);
    }

    /* External reset (channel change, new capture). */
    void reset() {
        state = RxState::Idle;
        bit_flip = 0;
        reset_frame_engine();
    }

   protected:
    /* Polarity variants of a sync word, applied to every dibit. */
    static constexpr uint64_t dibit_flip_mask48 = 0xAAAAAAAAAAAAULL;
    static constexpr uint32_t dibit_flip_mask24 = 0xAAAAAAAAUL;

    static constexpr size_t tch_bits = tch_frame_bits;
    static constexpr size_t cc24_bits = 24;

    static constexpr uint8_t sync_err_max_24 = 2;
    static constexpr uint8_t sync_err_max_48 = 5;
    static constexpr uint8_t cch_fail_limit = 8;

    enum class RxState : uint8_t {
        Idle,    /* sync word search */
        Header,  /* FS1/FS4: HI0, CC, HI1 */
        SFSync,  /* 24-bit frame prefix: FS2 / CC / FS3 */
        CCH,     /* 72-bit coded control channel */
        TCH,     /* 288-bit payload */
        End,     /* 72-bit end frame */
    };

    /* TCH payload delivery (9.1/9.2/9.3). fn is the CCH frame number.
     * on_voice_tch: raw vocoder bits, unscrambled. on_data_tch: type 1/2
     * payload after the combined descramble. on_tch_lost: CCH undecodable
     * and the data hypothesis failed (feed LOST to a vocoder). */
    virtual void on_voice_tch(uint8_t fn, const uint8_t raw[tch_bits]) = 0;
    virtual void on_data_tch(uint8_t fn, const uint8_t payload[tch_bits / 8], bool type2, const CchFields& f) = 0;
    virtual void on_tch_lost(uint8_t fn) = 0;

    /* Best-matching polarity variant of a sync word in the (raw) sync
     * register. Returns the popcount error, or -1 above the limit; sets
     * flip_out to the dibit XOR mask that normalises the stream. */
    static int match_sync(
        const uint64_t reg,
        const uint64_t word,
        const size_t word_bits,
        const uint8_t err_limit,
        uint8_t& flip_out) {
        const uint64_t word_mask =
            (word_bits >= 64) ? ~0ULL : ((1ULL << word_bits) - 1);
        const uint64_t w = word & word_mask;
        const uint64_t flip2 =
            (word_bits <= 32) ? dibit_flip_mask24 : dibit_flip_mask48;

        /* Polarity variants: raw, spectrum-inverted (dibit b1 flipped),
         * bit-complemented, and both. */
        const uint64_t variants[4] = {
            w,
            w ^ flip2,
            ~w & word_mask,
            (~w & word_mask) ^ flip2,
        };

        int best_err = err_limit + 1;
        int best = -1;
        for (size_t i = 0; i < 4; i++) {
            const int err = __builtin_popcountll(reg ^ variants[i]);
            if (err < best_err) {
                best_err = err;
                best = static_cast<int>(i);
            }
        }
        if (best < 0)
            return -1;

        static constexpr uint8_t flip_for_variant[4] = {0b00, 0b10, 0b11, 0b01};
        flip_out = flip_for_variant[best];
        return best_err;
    }

    void process_dibit(const uint8_t raw_dibit) {
        /* Sync register always follows the raw bit stream (clause 9.0: MSB
         * transmitted first); polarity is resolved by the sync matcher. */
        sync_register = (sync_register << 1) | ((raw_dibit >> 1) & 1u);
        sync_register = (sync_register << 1) | (raw_dibit & 1u);

        if (state == RxState::Idle) {
            handle_sync_search();
            return;
        }

        const uint8_t d = raw_dibit ^ bit_flip;
        collect_bit((d >> 1) & 1u);
        collect_bit(d & 1u);
    }

    void handle_sync_search() {
        uint8_t flip = 0;
        int err;

        err = match_sync(sync_register & 0xFFFFFF, fs2, 24, sync_err_max_24, flip);
        if (err >= 0) {
            bit_flip = flip;
            reset_frame_engine();
            state = RxState::CCH;
            start_collect(cch_fec_bits);
            return;
        }

        err = match_sync(sync_register & 0xFFFFFF, fs3, 24, sync_err_max_24, flip);
        if (err >= 0) {
            bit_flip = flip;
            reset_frame_engine();
            state = RxState::End;
            start_collect(end_fec_bits);
            return;
        }

        err = match_sync(sync_register & 0xFFFFFFFFFFFFULL, fs1, 48, sync_err_max_48, flip);
        if (err < 0)
            err = match_sync(sync_register & 0xFFFFFFFFFFFFULL, fs4, 48, sync_err_max_48, flip);
        if (err >= 0) {
            bit_flip = flip;
            reset_frame_engine();
            state = RxState::Header;
            header_phase = 0;
            start_collect(hi_fec_bits);
        }
    }

    void collect_bit(const uint8_t bit) {
        frame_bits[collect_count++] = bit & 1u;
        if (collect_count >= collect_target)
            handle_block_complete();
    }

    void start_collect(const size_t bits) {
        collect_count = 0;
        collect_target = static_cast<uint16_t>(bits);
    }

    void reset_frame_engine() {
        collect_count = 0;
        collect_target = 0;
        header_phase = 0;
        cch_fail_count = 0;
        hi0_valid = false;
        data_pending = false;
    }

    void handle_block_complete() {
        switch (state) {
            case RxState::Header:
                handle_header_complete();
                break;
            case RxState::SFSync:
                handle_sfsync_complete();
                break;
            case RxState::CCH:
                handle_cch_complete();
                break;
            case RxState::TCH:
                handle_tch_complete();
                break;
            case RxState::End:
                handle_end_complete();
                break;
            default:
                reset_frame_engine();
                state = RxState::Idle;
                break;
        }
    }

    /* Header frame (9.5): [FS1|FS4] [HI0 120] [CC 24] [HI1 120]; the copy with
     * the verifying CRC8 wins (the repetition protects against a burst over
     * one of them). */
    void handle_header_complete() {
        if (header_phase == 0) {
            hi0_result = decode_hi(frame_bits, hi0_decoded);
            hi0_valid = hi0_result.crc_ok;
            header_phase = 1;
            start_collect(cc24_bits);
            return;
        }

        if (header_phase == 1) {
            colour_code = channel_code_decode(frame_bits);
            header_phase = 2;
            start_collect(hi_fec_bits);
            return;
        }

        uint8_t hi1_decoded[hi_bits];
        const DecodeResult hi1_result = decode_hi(frame_bits, hi1_decoded);

        const bool use_hi0 = hi0_valid && !hi1_result.crc_ok;
        const HiFields f = unpack_hi(use_hi0 ? hi0_decoded : hi1_decoded);
        const DecodeResult res = use_hi0 ? hi0_result : hi1_result;

        DPMRFrameMessage message;
        message.frame_type = DPMRFrameMessage::HEADER;
        message.colour_code = colour_code;
        message.called_id = f.called_id;
        message.own_id = f.own_id;
        message.comm_mode = f.comm_mode;
        message.version = f.version;
        message.comm_format = f.comm_format;
        message.header_type = f.header_type;
        message.ci_type = f.ci_type;
        message.ci_info = f.ci_info;
        message.corrected = res.corrected;
        message.failed_blocks = res.failed_blocks;
        message.crc_ok = res.crc_ok;
        shared_memory.application_queue.push(message);

        /* The next 24 bits start the superframe (or end the call). */
        state = RxState::SFSync;
        start_collect(cc24_bits);
    }

    /* 24-bit frame prefix between frames: FS2 (frames 1 and 3), colour code
     * (frames 2 and 4, 9.1) or FS3 (end of call, 9.6). */
    void handle_sfsync_complete() {
        uint8_t flip = 0;
        int err = match_sync(sync_register & 0xFFFFFF, fs2, 24, sync_err_max_24, flip);
        if (err >= 0) {
            bit_flip = flip;
            state = RxState::CCH;
            start_collect(cch_fec_bits);
            return;
        }

        err = match_sync(sync_register & 0xFFFFFF, fs3, 24, sync_err_max_24, flip);
        if (err >= 0) {
            bit_flip = flip;
            state = RxState::End;
            start_collect(end_fec_bits);
            return;
        }

        /* Neither sync word: a colour code prefixing the next frame. */
        colour_code = channel_code_decode(frame_bits);
        state = RxState::CCH;
        start_collect(cch_fec_bits);
    }

    /* CCH (9.1/9.2/9.3): 41 bits + CRC7, 6 x Hamming(12,8), 12x6
     * interleaved. A fresh-scramble decode matches voice superframes; a
     * failure defers to the combined CCH+TCH decode of the data
     * superframes. The called/own ID arrives in 12-bit slices over
     * frames 0..3. */
    void handle_cch_complete() {
        for (size_t i = 0; i < cch_fec_bits; i++)
            last_cch_coded[i] = frame_bits[i];

        uint8_t decoded[cch_bits];
        const DecodeResult res = decode_cch(frame_bits, decoded);

        if (!res.crc_ok) {
            if (++cch_fail_count >= cch_fail_limit) {
                reset_engine_to_idle();
                return;
            }
            data_pending = true;
            state = RxState::TCH;
            start_collect(tch_bits);
            return;
        }

        cch_fail_count = 0;
        data_pending = false;
        accept_cch(decoded, res);

        state = RxState::TCH;
        start_collect(tch_bits);
    }

    void handle_tch_complete() {
        if (!data_pending) {
            on_voice_tch(last_cch_fn, frame_bits);
        } else {
            data_pending = false;

            uint8_t cch_dec[cch_bits];
            uint8_t payload[tch_bits / 8];
            DecodeResult res = decode_data_frame(last_cch_coded, frame_bits, false, cch_dec, payload);
            bool type2 = false;
            if (!res.crc_ok) {
                res = decode_data_frame(last_cch_coded, frame_bits, true, cch_dec, payload);
                type2 = true;
            }
            if (res.crc_ok) {
                cch_fail_count = 0;
                accept_cch(cch_dec, res);
                const CchFields f = unpack_cch(cch_dec);
                on_data_tch(last_cch_fn, payload, type2, f);
            } else {
                on_tch_lost(last_cch_fn);
            }
        }

        state = RxState::SFSync;
        start_collect(cc24_bits);
    }

    /* End frame (9.6): two repeated 36-bit coded copies after FS3. */
    void handle_end_complete() {
        uint8_t decoded[end_bits];
        const DecodeResult res = decode_end(frame_bits, decoded);
        const EndFields f = unpack_end(decoded);

        DPMRFrameMessage message;
        message.frame_type = DPMRFrameMessage::END;
        message.colour_code = colour_code;
        message.called_id = called_id;
        message.own_id = own_id;
        message.end_type = f.end_type;
        message.arq = f.arq;
        message.tx_wait = f.tx_wait;
        message.status = f.status;
        message.corrected = res.corrected;
        message.failed_blocks = res.failed_blocks;
        message.crc_ok = res.crc_ok;
        shared_memory.application_queue.push(message);

        state = RxState::Idle;
        bit_flip = 0;
    }

    /* Fields of a verified CCH: ID slice assembly, bookkeeping, message. */
    void accept_cch(const uint8_t decoded[cch_bits], const DecodeResult& res) {
        const CchFields f = unpack_cch(decoded);

        switch (f.frame_number) {
            case 0:
                called_id = (called_id & 0x00000FFFu) | (static_cast<uint32_t>(f.id) << 12);
                break;
            case 1:
                called_id = (called_id & 0xFFFFF000u) | f.id;
                break;
            case 2:
                own_id = (own_id & 0x00000FFFu) | (static_cast<uint32_t>(f.id) << 12);
                break;
            case 3:
                own_id = (own_id & 0xFFFFF000u) | f.id;
                break;
            default:
                break;
        }
        last_cch_fn = f.frame_number;
        last_comm_mode = f.comm_mode;
        last_cch_sld = f.sld;

        DPMRFrameMessage message;
        message.frame_type = DPMRFrameMessage::CCH;
        message.frame_number = f.frame_number;
        message.colour_code = colour_code;
        message.called_id = called_id;
        message.own_id = own_id;
        message.comm_mode = f.comm_mode;
        message.version = f.version;
        message.comm_format = f.comm_format;
        message.sld = f.sld;
        message.corrected = res.corrected;
        message.failed_blocks = res.failed_blocks;
        message.crc_ok = res.crc_ok;
        shared_memory.application_queue.push(message);
    }

    void reset_engine_to_idle() {
        state = RxState::Idle;
        bit_flip = 0;
        data_pending = false;
    }

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
    uint8_t frame_bits[tch_bits]{};
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
    bool data_pending{false};
    uint8_t last_cch_coded[cch_fec_bits]{};
    uint8_t last_comm_mode{0};
    uint32_t last_cch_sld{0};
};

#endif /*__PROC_DPMR_RX_H__*/
