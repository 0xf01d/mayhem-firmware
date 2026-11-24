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

/* dPMR446 signalling/data receiver (ETSI TS 102 490 V1.9.1):
 * 4FSK 2400 baud (12.2), sync/frame structure (6.3-6.5, 8, 9), layer-2
 * decoding through the FEC chains of clause 7/9. The frame engine lives in
 * proc_dpmr_rx.hpp and is shared with the voice receiver. */

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

class DPMRProcessor : public BasebandProcessor {
   public:
    DPMRProcessor();

    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

   private:
    static constexpr size_t baseband_fs = 3072000;
    static constexpr size_t channel_fs = DPMRRxEngine::channel_fs;

    /* TCH payloads surfaced to the application (9.1/9.2/9.3). */
    class Engine : public DPMRRxEngine {
       private:
        void on_voice_tch(uint8_t fn, const uint8_t raw[tch_bits]) override {
            DPMRTchDataMessage message;
            message.frame_number = fn;
            message.length = tch_bits / 8;
            message.decoded = 0; /* raw vocoder bits */
            dpmr::bits_to_bytes(raw, message.data, tch_bits / 8);
            shared_memory.application_queue.push(message);
        }

        void on_data_tch(uint8_t fn, const uint8_t payload[tch_bits / 8], bool type2, const CchFields& f) override {
            (void)f;
            DPMRTchDataMessage message;
            message.frame_number = fn;
            message.length = type2 ? t2_payload_bytes : tch_bits / 8;
            message.decoded = type2 ? 2 : 1;
            for (size_t i = 0; i < message.length; i++)
                message.data[i] = payload[i];
            shared_memory.application_queue.push(message);
        }

        void on_tch_lost(uint8_t fn) override {
            (void)fn; /* nothing to surface */
        }
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

#endif /*__PROC_DPMR444_H__*/
