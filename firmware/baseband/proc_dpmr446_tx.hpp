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

#ifndef __PROC_DPMR446_TX_H__
#define __PROC_DPMR446_TX_H__

/* dPMR446 transmitter (ETSI TS 102 490 V1.9.1): 4FSK 2400 baud (12.2,
 * SRRC r = 0.2 cascaded with FM), frame assembly per 4.2.2/9.x, and the
 * AMBE+2 voice path (xombe encoder, one 72-bit frame per 20 ms TCH block).
 *
 * Voice TX: preamble + header + superframes, PTT release completes the
 * superframe with vocoder silence frames and appends the End frame.
 * Data TX (type 1/2): payload bytes arrive in shared_memory.bb_data,
 * 288/160 bits per frame, CCH+TCH scrambled together (9.2/9.3).
 * Signalling TX: repeated header+end pairs (call set-up, ID, disconnect). */

#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "audio_input.hpp"
#include "dpmr.hpp"
#include "dpmr_mod.hpp"
#include "message.hpp"
#include "portapack_shared_memory.hpp"

#include <cstdint>
#include <memory>

using namespace dpmr;

class DPMRTxProcessor : public BasebandProcessor {
   public:
    DPMRTxProcessor();

    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

   private:
    static constexpr size_t baseband_fs = 1536000;
    static constexpr uint32_t symbol_rate = dpmr_mod::symbol_rate; /* 2400 */
    static constexpr size_t shaping_fs = 24000;                    /* SRRC rate */
    static constexpr size_t out_per_symbol = baseband_fs / symbol_rate;  /* 640 */
    static constexpr size_t out_per_shaping = baseband_fs / shaping_fs;  /* 64 */
    static constexpr size_t shaping_per_symbol = out_per_symbol / out_per_shaping; /* 10 */

    /* Initialized before the last-member BasebandThread can start execute(). */
    struct StateMutex {
        Mutex mutex{};
        StateMutex() { chMtxInit(&mutex); }
    } state_mutex{};

    bool configured{false};

    /* ---------------- transmit sequence state machine */
    enum class TxPhase : uint8_t {
        Idle,     /* waiting for a configuration */
        Preamble, /* >= 72 bits of 0x5F (6.1.6) */
        Header,   /* FS1 + HI0 + CC + HI1 (9.5) */
        Payload,  /* voice/data superframe frames (9.1/9.2/9.3) */
        End,      /* FS3 + END x2 (9.6) */
    };

    void start_tx(const DPMRTxConfigureMessage& cfg);
    void prepare_next_block();
    bool prepare_payload_frame();
    void fill_dibit_ring();
    void push_dibit(uint8_t dibit);
    int pop_dibit();
    void finish_tx();
    void emit_progress();

    TxPhase phase{TxPhase::Idle};

    /* Plain copy of the configure message: Message carries a const id
     * member, so message objects cannot be assigned after construction. */
    struct TxConfig {
        uint8_t mode{0};
        uint8_t header_type{0};
        uint8_t ci_type{0};
        uint8_t ci_info{0};
        uint8_t end_type{0};
        uint8_t arq{0};
        uint8_t tx_wait{0};
        uint8_t status{0};
        uint8_t repeat{1};
        uint8_t mic_shift{4};
        uint16_t mic_gain_q8{256};
        uint16_t payload_len{0};
        uint16_t sld{0};
    };
    TxConfig cfg{};
    TxCall call{};

    uint8_t frame_bits[payload_frame_bits]{};
    uint16_t bits_total{0};
    uint16_t bits_sent{0};
    uint8_t frame_number{0};       /* CCH FN of the next payload frame */
    uint16_t payload_bytes_sent{0};
    uint16_t signalling_pairs_left{0};
    bool end_requested{false};
    uint8_t voc_q[4][9]{};         /* vocoder frames awaiting the next TCH */
    uint8_t voc_q_n{0};
    uint16_t underruns{0};

    uint8_t dibit_ring[512]{};
    uint16_t ring_head{0}; /* producer */
    uint16_t ring_tail{0}; /* consumer */

    /* ---------------- SRRC shaping */
    /* Symbol levels (+/-3, +/-1 units) shaping history, newest last. */
    static constexpr size_t sym_hist_n = dpmr_mod::rrc_span_symbols * 2 + 1; /* 13 */
    float sym_hist[sym_hist_n]{};
    float shaped_buf[shaping_per_symbol]{}; /* one symbol of SRRC output */
    size_t out_tick{0};      /* 0..639, output samples per symbol */

    void push_symbol(float level);
    float srrc_sample(size_t n);

    /* ---------------- FM modulator */
    uint32_t phase_acc{0};
    uint32_t level_phase_step{0}; /* per output sample per level unit */

    float last_level{1.0f};

    /* ---------------- voice: mic -> xombe encoder */
    static constexpr size_t mic_buffer_n = 32; /* 750 Hz x 24 kHz */
    int16_t audio_data[mic_buffer_n]{};
    buffer_s16_t audio_buffer{audio_data, mic_buffer_n};
    AudioInput audio_input{};

    int16_t pcm_frame[160]{};
    uint16_t pcm_fill{0};
    uint32_t decim_acc{0};   /* fractional 24k -> 8k */

    /* xombe encoder state: 39.4 KiB (q15 build), static storage in the .cpp. */
    struct xombe_enc* enc{nullptr};
    bool enc_ready{false};

    int16_t feed_pcm(int16_t sample);

    TXProgressMessage txprogress_message{};

    /* NB: Threads should be the last members in the class definition. */
    BasebandThread baseband_thread{baseband_fs, this, baseband::Direction::Transmit};
};

#endif /*__PROC_DPMR446_TX_H__*/
