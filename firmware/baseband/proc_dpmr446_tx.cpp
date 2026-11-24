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

#include "proc_dpmr446_state.hpp"
#include "proc_dpmr446_tx.hpp"

#include "event_m4.hpp"
#include "audio_dma.hpp"
#include "sine_table_int8.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include "xombe.h"

namespace {
/* Encoder state in static storage: too large for the processor heap
 * allocation, fits M4 RAM beside the stacks.
 *
 * It lives in the .vocoder_state section, which the voice/TX linker script
 * places in Local SRAM Bank 1 next to the code. Bank 2 only has 32 KiB and the
 * encoder alone wants 39 KiB, so leaving it in .bss would not link.
 */
#if defined(DPMR446_DUAL_IMAGE)
/* The combined image shares one buffer with the receive half: only one
 * direction ever runs per boot. See proc_dpmr446_state.cpp. */
#define enc_mem dpmr446_vocoder_state
#else
__attribute__((aligned(8), section(".vocoder_state"))) uint8_t enc_mem[XOMBE_ENC_SIZE];
#endif

class StateLock {
   public:
    explicit StateLock(Mutex& mutex) { chMtxLock(&mutex); }
    ~StateLock() { chMtxUnlock(); }
    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;
};
}  // namespace

DPMRTxProcessor::DPMRTxProcessor() {
    level_phase_step = static_cast<uint32_t>(
        dpmr_mod::deviation_unit_hz * 4294967296.0f / static_cast<float>(baseband_fs));
    baseband_thread.start();
}

void DPMRTxProcessor::on_message(const Message* const msg) {
    const DPMRTxConfigureMessage cfg_msg = *reinterpret_cast<const DPMRTxConfigureMessage*>(msg);
    const RequestSignalMessage request_msg = *reinterpret_cast<const RequestSignalMessage*>(msg);

    switch (msg->id) {
        case Message::ID::DPMRTxConfigure: {
            const StateLock lock{state_mutex.mutex};
            start_tx(cfg_msg);
            break;
        }

        case Message::ID::RequestSignal:
            if (request_msg.signal == RequestSignalMessage::Signal::DPMRTxRelease) {
                const StateLock lock{state_mutex.mutex};
                end_requested = true;
            }
            break;

        default:
            break;
    }
}

void DPMRTxProcessor::start_tx(const DPMRTxConfigureMessage& c) {
    cfg.mode = c.mode;
    cfg.header_type = c.header_type;
    cfg.ci_type = c.ci_type;
    cfg.ci_info = c.ci_info;
    cfg.end_type = c.end_type;
    cfg.arq = c.arq;
    cfg.tx_wait = c.tx_wait;
    cfg.status = c.status;
    cfg.repeat = c.repeat;
    cfg.mic_shift = c.mic_shift;
    cfg.mic_gain_q8 = c.mic_gain_q8;
    /* bb_data.data is 512 B and this is the index bound for the data modes.
     * payload_len is uint8_t so it cannot exceed 255, but the check belongs
     * where the value enters rather than in the loop that trusts it. */
    cfg.payload_len = (c.payload_len > sizeof(shared_memory.bb_data.data))
                          ? sizeof(shared_memory.bb_data.data)
                          : c.payload_len;
    cfg.sld = c.sld;
    call.cc12 = c.cc12;
    call.called_id = c.called_id;
    call.own_id = c.own_id;
    call.comm_mode = c.comm_mode;
    call.version = c.version;
    call.comm_format = c.comm_format;

    phase = TxPhase::Idle;
    bits_total = 0;
    bits_sent = 0;
    frame_number = 0;
    payload_bytes_sent = 0;
    signalling_pairs_left = cfg.repeat ? cfg.repeat : 1;
    end_requested = false;
    voc_q_n = 0;
    underruns = 0;
    ring_head = 0;
    ring_tail = 0;
    for (auto& s : sym_hist)
        s = 0.0f;
    out_tick = 0;
    last_level = 1.0f;
    phase_acc = 0;
    pcm_fill = 0;
    decim_acc = 0;

    if (cfg.mode == DPMRTxConfigureMessage::VOICE) {
        xombe_enc_cfg ec;
        xombe_enc_cfg_default(&ec);
        ec.outputs = XOMBE_ENC_DMR;
        ec.tones = 0;
        ec.dmr_quant = XOMBE_ENC_QUANT_OPEN;
        enc = xombe_enc_init(enc_mem, &ec);
        enc_ready = (enc != nullptr);
        audio_input.read_audio_buffer(audio_buffer); /* drain stale mic samples */
    } else {
        enc_ready = false;
    }

    txprogress_message.progress = 0;
    txprogress_message.done = false;
    configured = true;
}

void DPMRTxProcessor::emit_progress() {
    txprogress_message.progress++;
    txprogress_message.done = false;
    shared_memory.application_queue.push(txprogress_message);
}

void DPMRTxProcessor::finish_tx() {
    txprogress_message.done = true;
    shared_memory.application_queue.push(txprogress_message);
    phase = TxPhase::Idle;
    configured = false;
    enc_ready = false;
}

bool DPMRTxProcessor::prepare_payload_frame() {
    if (cfg.mode == DPMRTxConfigureMessage::VOICE) {
        if (end_requested && frame_number == 0)
            return false; /* current superframe completed with silence */

        uint8_t voc[tch_frame_bits / 8];
        if (voc_q_n == tch_blocks_per_frame) {
            for (size_t k = 0; k < tch_blocks_per_frame; k++)
                memcpy(&voc[k * 9], voc_q[k], 9);
            voc_q_n = 0;
        } else {
            /* Vocoder behind: repeat the last set (or zeros at start). */
            underruns++;
            for (size_t k = 0; k < tch_blocks_per_frame; k++)
                memcpy(&voc[k * 9], voc_q_n ? voc_q[tch_blocks_per_frame - 1] : voc_q[0], 9);
        }
        encode_voice_frame(call, frame_number, voc, frame_bits);
        bits_total = payload_frame_bits;
        bits_sent = 0;
        if (frame_number == 3)
            emit_progress(); /* superframe boundary */
        frame_number = (frame_number + 1) & 3;
        return true;
    }

    /* Type 1/2 data: payload bytes from bb_data. */
    const uint16_t per_frame = (cfg.mode == DPMRTxConfigureMessage::DATA2) ? t2_payload_bytes : (tch_frame_bits / 8);
    if (payload_bytes_sent >= cfg.payload_len)
        return false;

    uint8_t chunk[tch_frame_bits / 8]{};
    const uint16_t n = std::min<uint16_t>(per_frame, cfg.payload_len - payload_bytes_sent);
    for (uint16_t i = 0; i < n; i++)
        chunk[i] = shared_memory.bb_data.data[payload_bytes_sent + i];
    payload_bytes_sent += per_frame;

    if (cfg.mode == DPMRTxConfigureMessage::DATA2)
        encode_data2_frame(call, frame_number, cfg.sld, chunk, frame_bits);
    else
        encode_data1_frame(call, frame_number, cfg.sld, chunk, frame_bits);
    bits_total = payload_frame_bits;
    bits_sent = 0;
    if (frame_number == 3)
        emit_progress();
    frame_number = (frame_number + 1) & 3;
    return true;
}

void DPMRTxProcessor::prepare_next_block() {
    switch (phase) {
        case TxPhase::Idle:
            phase = TxPhase::Preamble;
            put_preamble(frame_bits, 96);
            bits_total = 96;
            bits_sent = 0;
            break;

        case TxPhase::Preamble:
            phase = TxPhase::Header;
            encode_header_frame(call, cfg.header_type, cfg.ci_type, cfg.ci_info, frame_bits);
            bits_total = header_frame_bits;
            bits_sent = 0;
            break;

        case TxPhase::Header:
            frame_number = 0;
            payload_bytes_sent = 0;
            phase = TxPhase::Payload;
            /* A first payload frame can decline: empty data, or PTT released
             * before frame 0. Treat it exactly as the Payload case does, or the
             * phase machine sits in Payload with nothing to send and execute()
             * modulates the last level until the radio is torn down. */
            if (!prepare_payload_frame()) {
                phase = TxPhase::End;
                encode_end_frame(cfg.end_type, cfg.arq, cfg.tx_wait, cfg.status, frame_bits);
                bits_total = end_frame_total_bits;
                bits_sent = 0;
            }
            break;

        case TxPhase::Payload:
            if (!prepare_payload_frame()) {
                phase = TxPhase::End;
                encode_end_frame(cfg.end_type, cfg.arq, cfg.tx_wait, cfg.status, frame_bits);
                bits_total = end_frame_total_bits;
                bits_sent = 0;
            }
            break;

        case TxPhase::End:
            if (cfg.mode == DPMRTxConfigureMessage::SIGNALLING && signalling_pairs_left > 1) {
                signalling_pairs_left--;
                phase = TxPhase::Preamble;
                put_preamble(frame_bits, 96);
                bits_total = 96;
                bits_sent = 0;
            } else {
                finish_tx();
            }
            break;
    }
}

void DPMRTxProcessor::push_dibit(const uint8_t dibit) {
    dibit_ring[ring_head] = dibit;
    ring_head = (ring_head + 1) % (sizeof(dibit_ring));
}

int DPMRTxProcessor::pop_dibit() {
    if (ring_tail == ring_head)
        return -1;
    const uint8_t d = dibit_ring[ring_tail];
    ring_tail = (ring_tail + 1) % (sizeof(dibit_ring));
    return d;
}

void DPMRTxProcessor::fill_dibit_ring() {
    /* Keep one slot free to distinguish full from empty. */
    while (((ring_head + 1) % sizeof(dibit_ring)) != ring_tail) {
        if (bits_sent >= bits_total) {
            prepare_next_block();
            if (!configured || bits_sent >= bits_total)
                return;
        }
        const uint8_t b1 = frame_bits[bits_sent++];
        const uint8_t b2 = frame_bits[bits_sent++];
        push_dibit(static_cast<uint8_t>((b1 << 1) | b2));
    }
}

void DPMRTxProcessor::push_symbol(const float level) {
    for (size_t i = 0; i + 1 < sym_hist_n; i++)
        sym_hist[i] = sym_hist[i + 1];
    sym_hist[sym_hist_n - 1] = level;
}

float DPMRTxProcessor::srrc_sample(const size_t n) {
    /* y[n] = sum_k h[k] * X[n-k]: X holds each symbol level across its 10
     * shaping samples, hist[12] is the newest symbol, and the 121 taps
     * span 12 symbols back plus a partial group on both boundary symbols. */
    float acc = 0.0f;
    size_t k = 0;
    for (; k <= n; k++)
        acc += dpmr_mod::rrc_taps[k] * sym_hist[12];
    for (size_t d = 1; d <= 11; d++) {
        float group = 0.0f;
        for (size_t j = 0; j < 10; j++)
            group += dpmr_mod::rrc_taps[k++];
        acc += group * sym_hist[12 - d];
    }
    float group = 0.0f;
    for (; k < dpmr_mod::rrc_tap_count; k++)
        group += dpmr_mod::rrc_taps[k];
    acc += group * sym_hist[0];
    return acc;
}

int16_t DPMRTxProcessor::feed_pcm(int16_t sample) {
    pcm_frame[pcm_fill++] = sample;
    if (pcm_fill == 160) {
        pcm_fill = 0;
        uint8_t out[9];
        if (enc_ready && xombe_enc_frame(enc, end_requested ? nullptr : pcm_frame, nullptr, out, nullptr) >= 0) {
            if (voc_q_n < tch_blocks_per_frame) {
                memcpy(voc_q[voc_q_n], out, 9);
                voc_q_n++;
            }
        }
    }
    return sample;
}

void DPMRTxProcessor::execute(const buffer_c8_t& buffer) {
    const StateLock lock{state_mutex.mutex};
    if (!configured) {
        for (size_t i = 0; i < buffer.count; i++)
            buffer.p[i] = {0, 0};
        return;
    }

    /* Voice: mic 24 kHz -> 8 kHz -> xombe, one 72-bit frame per 20 ms. */
    if (cfg.mode == DPMRTxConfigureMessage::VOICE && enc_ready) {
        if (!end_requested)
            audio_input.read_audio_buffer(audio_buffer);

        int16_t a1 = 0, b1 = 0, a2 = 0, b2 = 0;
        for (size_t i = 0; i < mic_buffer_n; i++) {
            const int32_t raw = end_requested ? 0 : audio_buffer.p[i];
            const int16_t s = static_cast<int16_t>(
                std::clamp<int32_t>(((raw >> cfg.mic_shift) * cfg.mic_gain_q8) >> 8, -32768, 32767));
            /* Two cascaded 3-sample boxcars, decimate by 3 (24k -> 8k). */
            const int32_t y1 = a1 + b1 + s;
            a1 = b1;
            b1 = s;
            if (++decim_acc % 3 == 0) {
                const int32_t y2 = a2 + b2 + y1;
                a2 = b2;
                b2 = y1;
                feed_pcm(static_cast<int16_t>(std::clamp<int32_t>(y2 / 9, -32768, 32767)));
            }
        }
    }

    for (size_t i = 0; i < buffer.count; i++) {
        if (out_tick == 0) {
            /* Symbol boundary: top up the dibit ring, pop one symbol, and
             * compute its (6-symbol-delayed) shaped waveform. */
            fill_dibit_ring();
            const int d = pop_dibit();
            const float level = (d >= 0) ? static_cast<float>(dpmr_mod::dibit_deviation_units(d)) : last_level;
            last_level = level;
            push_symbol(level);
            for (size_t n = 0; n < shaping_per_symbol; n++)
                shaped_buf[n] = srrc_sample(n);
        }

        const size_t s = out_tick >> 6; /* 0..9 shaping samples per symbol */
        const float frac = static_cast<float>(out_tick & 63) / 64.0f;
        const float a = shaped_buf[s];
        const float b = (s + 1 < shaping_per_symbol) ? shaped_buf[s + 1] : a;
        const float value = a + (b - a) * frac;

        phase_acc += static_cast<uint32_t>(static_cast<int32_t>(value * static_cast<float>(level_phase_step)));

        buffer.p[i] = {
            sine_table_i8[(phase_acc + 0x40000000u) >> 24],
            sine_table_i8[phase_acc >> 24]};

        if (++out_tick == out_per_symbol)
            out_tick = 0;
    }
}

int dpmr446_tx_entry() {
    audio::dma::init_audio_in();

    EventDispatcher event_dispatcher{std::make_unique<DPMRTxProcessor>()};
    event_dispatcher.run();
    return 0;
}


