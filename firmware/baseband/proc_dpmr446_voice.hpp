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

#ifndef __PROC_DPMR446_VOICE_H__
#define __PROC_DPMR446_VOICE_H__

/* dPMR446 voice receiver (ETSI TS 102 490 V1.9.1): the signalling receiver's
 * front end (3-stage decimation to 48 kHz, channel filter, FM demodulator)
 * and frame engine, with the TCH payload fed through the AMBE+2 vocoder.
 * One TCH is four 72-bit AMBE+2 frames (9.1), 640 samples of 8 kHz PCM per
 * 80 ms; they are linearly upsampled to the 24 kHz audio codec rate. */

#include "audio_output.hpp"
#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "dsp_decimate.hpp"
#include "dsp_demodulate.hpp"
#include "dsp_fir_taps.hpp"
#include "proc_dpmr_rx.hpp"
#include "rssi_thread.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct xombe_dec;

class DPMRVoiceProcessor : public BasebandProcessor {
   public:
    DPMRVoiceProcessor();

    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

   private:
    static constexpr size_t baseband_fs = 3072000;
    static constexpr size_t channel_fs = DPMRRxEngine::channel_fs;

    /* TCH -> xombe decoder -> audio::output. The CCH/header/end messages are
     * produced by the shared engine, so this image also drives the decode
     * console; only the TCH handling differs from the signalling receiver. */
    class Engine : public DPMRRxEngine {
       public:
        Engine();

       private:
        void on_voice_tch(uint8_t fn, const uint8_t raw[tch_bits]) override;
        void on_data_tch(uint8_t fn, const uint8_t payload[tch_bits / 8], bool type2, const CchFields& f) override;
        void on_tch_lost(uint8_t fn) override;

        void decode_tch_frame(uint8_t fn, const uint8_t frame_bytes[9], unsigned flags);
        void write_pcm(const int16_t* pcm);

        xombe_dec* decoder_{nullptr};
        AudioOutput audio_output_{};
        int16_t prev_pcm_{0};
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

    Engine engine{};

    BasebandThread baseband_thread{baseband_fs, this, baseband::Direction::Receive, false};
    RSSIThread rssi_thread{};
};

#endif /*__PROC_DPMR446_VOICE_H__*/
