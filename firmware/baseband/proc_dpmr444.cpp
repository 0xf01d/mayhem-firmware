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

#include "proc_dpmr444.hpp"

#include "event_m4.hpp"
#include "portapack_shared_memory.hpp"

#include <cmath>
#include <memory>

DPMRProcessor::DPMRProcessor() {
    decim_0.configure(taps_dpmr_decim_0.taps);
    decim_1.configure(taps_dpmr_decim_1.taps);
    channel_filter.configure(taps_dpmr_channel.taps, 1);
    demod.configure(channel_fs, 1050.0f); /* maximum deviation, table 12.1 */

    baseband_thread.start();
    configured = true;
}

void DPMRProcessor::execute(const buffer_c8_t& buffer) {
    if (!configured) return;

    const auto decim_0_out = decim_0.execute(buffer, dst_buffer_0);
    const auto decim_1_out = decim_1.execute(decim_0_out, dst_buffer_1);

    feed_channel_stats(decim_1_out);

    const auto filtered = channel_filter.execute(decim_1_out, dst_buffer_1);
    const auto audio_out = demod.execute(filtered, audio_buffer);

    for (size_t i = 0; i < audio_out.count; i++)
        on_symbol(audio_out.p[i]);
}

void DPMRProcessor::on_message(const Message* const message) {
    (void)message; /* fixed 4FSK/2400 baud configuration, nothing to set */
}

int DPMRProcessor::match_sync(
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

void DPMRProcessor::on_symbol(const float value) {
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
        if (std::fabs(m - level_of_q[q]) > std::fabs(m - level_of_q[last_q]))
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

void DPMRProcessor::process_dibit(const uint8_t raw_dibit) {
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

void DPMRProcessor::handle_sync_search() {
    uint8_t flip = 0;
    int err;

    err = match_sync(sync_register & 0xFFFFFF, fs2, 24, sync_err_max_24, flip);
    if (err >= 0) {
        bit_flip = flip;
        reset_frame_engine();
        state = RxState::CCH;
        start_collect(cch_frame_bits);
        return;
    }

    err = match_sync(sync_register & 0xFFFFFF, fs3, 24, sync_err_max_24, flip);
    if (err >= 0) {
        bit_flip = flip;
        reset_frame_engine();
        state = RxState::End;
        start_collect(end_frame_bits);
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
        start_collect(hi_block_bits);
    }
}

void DPMRProcessor::collect_bit(const uint8_t bit) {
    frame_bits[collect_count++] = bit & 1u;
    if (collect_count >= collect_target)
        handle_block_complete();
}

void DPMRProcessor::start_collect(const size_t bits) {
    collect_count = 0;
    collect_target = static_cast<uint16_t>(bits);
}

void DPMRProcessor::reset_frame_engine() {
    collect_count = 0;
    collect_target = 0;
    header_phase = 0;
    cch_fail_count = 0;
    hi0_valid = false;
}

void DPMRProcessor::handle_block_complete() {
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
void DPMRProcessor::handle_header_complete() {
    if (header_phase == 0) {
        hi0_result = decode_hi(frame_bits, hi0_decoded);
        hi0_valid = hi0_result.crc_ok;
        header_phase = 1;
        start_collect(cc_bits);
        return;
    }

    if (header_phase == 1) {
        colour_code = channel_code_decode(frame_bits);
        header_phase = 2;
        start_collect(hi_block_bits);
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
    start_collect(cc_bits);
}

/* 24-bit frame prefix between frames: FS2 (frames 1 and 3), colour code
 * (frames 2 and 4, 9.1) or FS3 (end of call, 9.6). */
void DPMRProcessor::handle_sfsync_complete() {
    uint8_t flip = 0;
    int err = match_sync(sync_register & 0xFFFFFF, fs2, 24, sync_err_max_24, flip);
    if (err >= 0) {
        bit_flip = flip;
        state = RxState::CCH;
        start_collect(cch_frame_bits);
        return;
    }

    err = match_sync(sync_register & 0xFFFFFF, fs3, 24, sync_err_max_24, flip);
    if (err >= 0) {
        bit_flip = flip;
        state = RxState::End;
        start_collect(end_frame_bits);
        return;
    }

    /* Neither sync word: a colour code prefixing the next frame. */
    colour_code = channel_code_decode(frame_bits);
    state = RxState::CCH;
    start_collect(cch_frame_bits);
}

/* CCH (9.1): 41 bits + CRC7, 6 x Hamming(12,8), 12x6 interleaved.
 * The called/own ID arrives in 12-bit slices over frames 0..3. */
void DPMRProcessor::handle_cch_complete() {
    uint8_t decoded[cch_bits];
    const DecodeResult res = decode_cch(frame_bits, decoded);
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

    if (!res.crc_ok) {
        if (++cch_fail_count >= cch_fail_limit) {
            state = RxState::Idle;
            bit_flip = 0;
            return;
        }
    } else {
        cch_fail_count = 0;
    }

    state = RxState::TCH;
    start_collect(tch_frame_bits);
}

/* TCH payload (9.1/9.2/9.3): 288 scrambled bits (vocoder data, type 1
 * uncorrected data, or 4 coded type-2 blocks). Surfaced raw; the vocoder
 * payload is not decoded (AMBE+2, out of scope). */
void DPMRProcessor::handle_tch_complete() {
    uint8_t work[tch_frame_bits];
    for (size_t i = 0; i < tch_frame_bits; i++)
        work[i] = frame_bits[i] & 1u;
    scramble(work, tch_frame_bits);

    DPMRTchDataMessage message;
    message.frame_number = last_cch_fn;
    message.length = tch_frame_bits / 8;
    for (size_t i = 0; i < tch_frame_bits / 8; i++) {
        uint8_t byte = 0;
        for (size_t b = 0; b < 8; b++)
            byte = (byte << 1) | work[i * 8 + b];
        message.data[i] = byte;
    }
    shared_memory.application_queue.push(message);

    state = RxState::SFSync;
    start_collect(cc_bits);
}

/* End frame (9.6): two repeated 36-bit coded copies after FS3. */
void DPMRProcessor::handle_end_complete() {
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

int main() {
    EventDispatcher event_dispatcher{std::make_unique<DPMRProcessor>()};
    event_dispatcher.run();
    return 0;
}
