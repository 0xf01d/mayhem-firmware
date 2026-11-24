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

#include "ui_dpmr446.hpp"

/* The voice images are 42-47 KiB compressed and SPI flash has 13.8 KiB free on a
 * full build, so they ship next to the app on the SD card instead of inside the
 * firmware. See baseband::run_image_from_file(). */
constexpr const char16_t voice_image_path[] = u"/APPS/dpmr446/PDMV.bin";
constexpr const char16_t tx_image_path[] = u"/APPS/dpmr446/PDMT.bin";

#include "audio.hpp"
#include "baseband_api.hpp"
#include "string_format.hpp"

using namespace portapack;
using namespace std::literals;

namespace ui::external_app::dpmr446 {

namespace {

std::vector<OptionsField::option_t> channel_options() {
    std::vector<OptionsField::option_t> options;
    for (size_t ch = 1; ch <= ::dpmr446::channel_count; ch++)
        options.emplace_back(to_string_dec_uint(ch), static_cast<int32_t>(ch));
    return options;
}

std::string hex6(const uint32_t value) {
    return to_string_hex(value, 6);
}

}  // namespace

void DPMR446View::focus() {
    field_frequency.focus();
}

DPMR446View::DPMR446View(NavigationView& nav)
    : nav_{nav} {
    add_children({
        &rssi,
        &field_rf_amp,
        &field_lna,
        &field_vga,
        &field_volume,
        &field_frequency,
        &options_direction,
        &labels,
        &options_channel,
        &text_code,
        &text_ids,
        &text_frame,
        &text_info,
        &console,
    });

    console.enable_scrolling(true);

    options_channel.on_change = [this](size_t, int32_t value) {
        channel_ = static_cast<uint32_t>(value);
        set_channel_frequency();
    };

    rebuild_channel_options();
    options_channel.set_by_value(channel_);
    set_channel_frequency();

    if (direction_ > 2)
        direction_ = 0;
    options_direction.set_by_value(direction_);
    options_direction.on_change = [this](size_t, int32_t value) {
        direction_ = static_cast<uint32_t>(value);
        apply_mode();
    };

    button_ptt.on_select = [this](Button&) {
        if (transmitting_ || stopping_)
            request_stop_tx();
        else if (direction_ == 2)
            start_tx();
    };

    signal_token_tick_second = rtc_time::signal_tick_second += [this]() {
        on_tick_second();
    };

    apply_mode();
}

DPMR446View::~DPMR446View() {
    rtc_time::signal_tick_second -= signal_token_tick_second;
    transmitting_ = false;
    stopping_ = false;
    transmitter_model.disable();
    receiver_model.disable();
    baseband::shutdown();
    audio::input::stop();
    audio::output::stop();
}

void DPMR446View::rebuild_channel_options() {
    options_channel.set_options(channel_options());
    if (channel_ < 1 || channel_ > ::dpmr446::channel_count)
        channel_ = 1;
    options_channel.set_by_value(channel_);
    set_channel_frequency();
}

void DPMR446View::set_channel_frequency() {
    field_frequency.set_value(::dpmr446::channel_frequency_hz(channel_));
}

void DPMR446View::apply_mode() {
    audio::output::stop();
    audio::input::stop();
    receiver_model.disable();
    transmitter_model.disable();
    transmitting_ = false;
    baseband::shutdown();

    text_code.set("-");
    text_ids.set("-");
    text_frame.set("-");
    text_info.set("-");
    console.clear(true);

    if (direction_ == 2) {
        /* TX: nothing runs until PTT. The image is loaded by start_tx(), which
         * is the only place that knows the microphone is about to be read. */
        text_info.set("TX ch" + to_string_dec_uint(channel_) + " PTT");
        return;
    }

    receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
    receiver_model.set_squelch_level(0);
    receiver_model.set_sampling_rate(3072000);
    receiver_model.set_baseband_bandwidth(1750000);

    if (direction_ == 0) {
        /* Voice RX: 4FSK demodulator, AMBE+2 decoder, audio out. */
        if (!baseband::run_image_from_file(voice_image_path)) {
            /* Say so rather than pretend the receiver is running when
             * hackrf.img is what got loaded. */
            text_info.set("RX voice image missing");
            return;
        }
        audio::set_rate(audio::Rate::Hz_24000);
        audio::output::start();
        text_info.set("RX voice ch" + to_string_dec_uint(channel_));
    } else {
        /* Signalling monitor: layer-2 frames on the console. Bank 2 holds the
         * app's own M0 code, so a voice image may have overwritten it: reload
         * the bundled baseband first, which is what run_image() does when the
         * chunk is not in the internal set. */
        baseband::run_image(portapack::spi_flash::image_tag_dpmr);
        text_info.set("RX monitor ch" + to_string_dec_uint(channel_));
    }

    receiver_model.enable();
}

void DPMR446View::start_tx() {
    if (direction_ != 2 || transmitting_ || stopping_)
        return;

    /* The transmit image has to be running before the model is enabled: it
     * owns the DMA and reads the microphone as soon as the thread starts. */
    audio::output::stop();
    receiver_model.disable();
    baseband::shutdown();

    if (!baseband::run_image_from_file(tx_image_path)) {
        /* Refuse to key the radio with no baseband behind it: enabling the
         * transmitter here would put whatever the M0 last wrote to the DMA
         * descriptors on air. */
        text_info.set("TX image missing");
        return;
    }

    /* The image waits for this before it modulates anything; without it
     * DPMRTxProcessor::configured stays false. */
    /* Voice mode, everything else left at the defaults the message already
     * carries (mic_shift 4, mic_gain_q8 256, one header/end pair). */
    DPMRTxConfigureMessage configure;
    configure.mode = DPMRTxConfigureMessage::VOICE;
    shared_memory.application_queue.push(configure);

    transmitter_model.set_target_frequency(::dpmr446::channel_frequency_hz(channel_));
    transmitter_model.set_sampling_rate(1536000);
    transmitter_model.set_baseband_bandwidth(1750000);
    transmitter_model.enable();

    transmitting_ = true;
    stopping_ = false;
    text_info.set("TX ch" + to_string_dec_uint(channel_));
}

void DPMR446View::request_stop_tx() {
    if (!transmitting_ || stopping_)
        return;

    /* Release the PTT: the image finishes the superframe in progress, sends its
     * End frame, and reports done. Tearing the radio down here would drop the
     * carrier mid-superframe and the call would end without an End. */
    const RequestSignalMessage signal{RequestSignalMessage::Signal::DPMRTxRelease};
    shared_memory.application_queue.push(signal);

    stopping_ = true;
    stop_ticks_ = 0;
    text_info.set("TX ch" + to_string_dec_uint(channel_) + " end...");
}

void DPMR446View::finish_stop_tx() {
    transmitter_model.disable();
    baseband::shutdown();
    transmitting_ = false;
    stopping_ = false;
    text_info.set("TX ch" + to_string_dec_uint(channel_) + " PTT");
}

void DPMR446View::on_tx_progress(const bool done) {
    if (done && stopping_)
        finish_stop_tx();
}

void DPMR446View::on_tick_second() {
    if (!stopping_)
        return;

    /* The image was asked to send its End frame and has not reported done.
     * Release the radio anyway rather than leave it keyed: a truncated call is
     * better than a stuck carrier. */
    if (++stop_ticks_ >= stop_timeout_s)
        finish_stop_tx();
}

void DPMR446View::on_frame(const DPMRFrameMessage& message) {
    const std::string err = to_string_dec_uint(message.corrected) + "/" +
                            to_string_dec_uint(message.failed_blocks) +
                            (message.crc_ok ? " OK" : " BAD");

    if (message.frame_type == DPMRFrameMessage::HEADER) {
        std::string ci{"-"};
        if (message.header_type == 0 || message.header_type == 1)
            ci = to_string_dec_uint(message.ci_type) + "/" +
                 to_string_dec_uint(message.ci_info, 3, '0');

        std::string called{hex6(message.called_id)};
        const int32_t common = ::dpmr446::isf_common_id(message.called_id);
        if (common >= 0)
            called += "(" + to_string_dec_uint(static_cast<uint32_t>(common), 3, '0') + ")";

        text_ids.set("C:" + called + " O:" + hex6(message.own_id));
        text_code.set(to_string_hex(message.colour_code, 3));
        text_frame.set("HDR T:" + to_string_dec_uint(message.header_type) +
                       " M:" + to_string_dec_uint(message.comm_mode, 3, '0') +
                       " F:" + to_string_dec_uint(message.comm_format, 2, '0') +
                       " E:" + err);
        text_info.set("CI:" + ci +
                      " V:" + to_string_dec_uint(message.version) +
                      " CC:" + to_string_hex(message.colour_code, 3));
        console.write("HDR T" + to_string_dec_uint(message.header_type) +
                      " C=" + hex6(message.called_id) +
                      " O=" + hex6(message.own_id) +
                      " E:" + err + "\n");
        return;
    }

    if (message.frame_type == DPMRFrameMessage::END) {
        text_frame.set("END T:" + to_string_dec_uint(message.end_type) +
                       " ARQ:" + to_string_dec_uint(message.arq, 2, '0') +
                       " WAIT:" + to_string_dec_uint(message.tx_wait) +
                       " STAT:" + to_string_dec_uint(message.status, 2, '0'));
        text_info.set("END status: " + to_string_dec_uint(message.status) +
                      " E:" + err);
        console.write("END T" + to_string_dec_uint(message.end_type) +
                      " S" + to_string_dec_uint(message.status) +
                      " E:" + err + "\n");
        return;
    }

    /* CCH frame */
    std::string called{hex6(message.called_id)};
    const int32_t common = ::dpmr446::isf_common_id(message.called_id);
    if (common >= 0)
        called += "(" + to_string_dec_uint(static_cast<uint32_t>(common), 3, '0') + ")";

    text_ids.set("C:" + called + " O:" + hex6(message.own_id));
    text_code.set(to_string_hex(message.colour_code, 3));
    text_frame.set("FN:" + to_string_dec_uint(message.frame_number) +
                   " M:" + to_string_dec_uint(message.comm_mode, 3, '0') +
                   " V:" + to_string_dec_uint(message.version) +
                   " F:" + to_string_dec_uint(message.comm_format, 2, '0') +
                   " E:" + err);
    text_info.set("CC:" + to_string_hex(message.colour_code, 3) +
                  " SLD:" + to_string_hex(message.sld, 5));
}

void DPMR446View::on_tch(const DPMRTchDataMessage& message) {
    if (message.frame_number != 0)
        return; /* one dump per superframe keeps the console readable */

    console.write("TCH FN0:");
    for (size_t i = 0; i < message.length; i++)
        console.write(to_string_hex(message.data[i], 2));
    console.write("\n");
}

}  // namespace ui::external_app::dpmr446
