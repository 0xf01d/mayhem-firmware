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
#include "proc_dpmr446_voice.hpp"

#include "audio_dma.hpp"
#include "event_m4.hpp"
#include "portapack_shared_memory.hpp"

#include <array>
#include <cstring>
#include <memory>

#include "xombe.h"

namespace {
/* Decoder state in static storage: 5248 B, too large for the processor heap
 * allocation but small enough to fit M4 RAM beside the stacks. */
#if defined(DPMR446_DUAL_IMAGE)
/* Shared with the transmit half; see proc_dpmr446_state.cpp. */
#define dec_mem dpmr446_vocoder_state
#else
__attribute__((aligned(8))) uint8_t dec_mem[XOMBE_DEC_SIZE];
#endif

constexpr size_t pcm_frame_samples = 160;    /* 20 ms at 8 kHz */
constexpr size_t audio_frame_samples = 480;  /* 20 ms at 24 kHz */
}  // namespace

DPMRVoiceProcessor::Engine::Engine() {
    decoder_ = xombe_dec_init(dec_mem, XOMBE_DMR);
    audio_output_.configure(iir_config_passthrough, iir_config_passthrough, 0.0f);
}

void DPMRVoiceProcessor::Engine::decode_tch_frame(
    const uint8_t fn,
    const uint8_t frame_bytes[XOMBE_FRAME_BYTES],
    const unsigned flags) {
    if (!decoder_)
        return;

    int16_t pcm[pcm_frame_samples];
    xombe_dec_frame(decoder_, frame_bytes, flags, pcm);

    xombe_decision decision;
    xombe_dec_decision_get(decoder_, &decision);

    DPMRVoiceStatusMessage status;
    status.frame_number = fn;
    status.concealing = decision.conceal != XOMBE_CONCEAL_NONE;
    status.vocoded = decision.cls == XOMBE_CLASS_VOICE;
    status.flags = 0;
    if (decision.cls == XOMBE_CLASS_ERASURE)
        status.flags |= XOMBE_FLAG_LOST;
    if (decision.bf)
        status.flags |= XOMBE_FLAG_BF;
    if (decision.hb)
        status.flags |= XOMBE_FLAG_HB;
    shared_memory.application_queue.push(status);

    write_pcm(pcm);
}

void DPMRVoiceProcessor::Engine::write_pcm(const int16_t* pcm) {
    /* 8 kHz vocoder PCM -> 24 kHz audio codec, linear x3 interpolation. The
     * phase is kept across frames so the stream stays continuous. */
    std::array<int16_t, audio_frame_samples> out;
    for (size_t i = 0; i < pcm_frame_samples; i++) {
        const int32_t a = prev_pcm_;
        const int32_t b = pcm[i];
        out[i * 3 + 0] = static_cast<int16_t>(a + (b - a) / 3);
        out[i * 3 + 1] = static_cast<int16_t>(a + (b - a) * 2 / 3);
        out[i * 3 + 2] = static_cast<int16_t>(b);
        prev_pcm_ = pcm[i];
    }
    const buffer_s16_t audio_buffer{out.data(), out.size(), channel_fs};
    audio_output_.write(audio_buffer);
}

void DPMRVoiceProcessor::Engine::on_voice_tch(
    const uint8_t fn,
    const uint8_t raw[tch_bits]) {
    /* 288 raw vocoder bits: four 72-bit AMBE+2 frames, MSB first (9.1). */
    uint8_t bytes[tch_frame_bits / 8];
    bits_to_bytes(raw, bytes, tch_frame_bits / 8);
    for (size_t k = 0; k < tch_blocks_per_frame; k++)
        decode_tch_frame(fn, &bytes[k * XOMBE_FRAME_BYTES], 0);
}

void DPMRVoiceProcessor::Engine::on_data_tch(
    const uint8_t fn,
    const uint8_t payload[tch_bits / 8],
    const bool type2,
    const CchFields& f) {
    /* Voice image: a data superframe has nothing for the vocoder, feed the
     * four slots as lost so concealment continues across the gap. */
    (void)payload;
    (void)type2;
    (void)f;
    for (size_t k = 0; k < tch_blocks_per_frame; k++)
        decode_tch_frame(fn, nullptr, XOMBE_FLAG_LOST);
}

void DPMRVoiceProcessor::Engine::on_tch_lost(const uint8_t fn) {
    for (size_t k = 0; k < tch_blocks_per_frame; k++)
        decode_tch_frame(fn, nullptr, XOMBE_FLAG_LOST);
}

DPMRVoiceProcessor::DPMRVoiceProcessor() {
    decim_0.configure(taps_dpmr_decim_0.taps);
    decim_1.configure(taps_dpmr_decim_1.taps);
    channel_filter.configure(taps_dpmr_channel.taps, 1);
    demod.configure(channel_fs, 1050.0f); /* maximum deviation, table 12.1 */

    baseband_thread.start();
    configured = true;
}

void DPMRVoiceProcessor::execute(const buffer_c8_t& buffer) {
    if (!configured) return;

    const auto decim_0_out = decim_0.execute(buffer, dst_buffer_0);
    const auto decim_1_out = decim_1.execute(decim_0_out, dst_buffer_1);

    feed_channel_stats(decim_1_out);

    const auto filtered = channel_filter.execute(decim_1_out, dst_buffer_1);
    const auto audio_out = demod.execute(filtered, audio_buffer);

    for (size_t i = 0; i < audio_out.count; i++)
        engine.on_symbol(audio_out.p[i]);
}

void DPMRVoiceProcessor::on_message(const Message* const message) {
    (void)message; /* fixed 4FSK/2400 baud configuration, nothing to set */
}

int dpmr446_voice_entry() {
    audio::dma::init_audio_out();

    EventDispatcher event_dispatcher{std::make_unique<DPMRVoiceProcessor>()};
    event_dispatcher.run();
    return 0;
}


