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

#include "ui_pmr446.hpp"

#include "audio.hpp"
#include "baseband_api.hpp"
#include "string_format.hpp"

using namespace portapack;
using namespace std::literals;

namespace ui::external_app::pmr446 {

namespace {

std::vector<OptionsField::option_t> channel_options(const bool digital) {
    std::vector<OptionsField::option_t> options;
    const size_t count = digital ? ::pmr446::channel_count : ::pmr446::analog_channel_count;
    for (size_t ch = 1; ch <= count; ch++)
        options.emplace_back(to_string_dec_uint(ch), static_cast<int32_t>(ch));
    return options;
}

std::string hex6(const uint32_t value) {
    return to_string_hex(value, 6);
}

}  // namespace

void PMR446View::focus() {
    field_frequency.focus();
}

PMR446View::PMR446View(NavigationView& nav)
    : nav_{nav} {
    add_children({
        &rssi,
        &field_rf_amp,
        &field_lna,
        &field_vga,
        &field_volume,
        &field_frequency,
        &labels,
        &options_mode,
        &options_channel,
        &text_code,
        &text_ids,
        &text_frame,
        &text_info,
        &console,
    });

    console.enable_scrolling(true);

    options_mode.on_change = [this](size_t, int32_t value) {
        digital_ = (value != 0);
        rebuild_channel_options();
        apply_mode();
    };

    options_channel.on_change = [this](size_t, int32_t value) {
        channel_ = static_cast<uint32_t>(value);
        set_channel_frequency();
    };

    options_mode.set_by_value(digital_);
    rebuild_channel_options();
    options_channel.set_by_value(channel_);
    set_channel_frequency();

    apply_mode();
}

PMR446View::~PMR446View() {
    receiver_model.disable();
    baseband::shutdown();
    audio::output::stop();
}

void PMR446View::rebuild_channel_options() {
    options_channel.set_options(channel_options(digital_ != 0));
    if (channel_ < 1 || channel_ > (digital_ ? ::pmr446::channel_count : ::pmr446::analog_channel_count))
        channel_ = 1;
    options_channel.set_by_value(channel_);
    set_channel_frequency();
}

void PMR446View::set_channel_frequency() {
    const uint32_t frequency = digital_
                                   ? ::pmr446::channel_frequency_hz(channel_)
                                   : ::pmr446::analog_frequency_hz(channel_);
    field_frequency.set_value(frequency);
}

void PMR446View::apply_mode() {
    audio::output::stop();
    receiver_model.disable();
    baseband::shutdown();

    if (digital_) {
        baseband::run_image(portapack::spi_flash::image_tag_dpmr);
        receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
        receiver_model.set_squelch_level(0);
    } else {
        /* Analogue PMR446: 12.5 kHz NFM voice (nbfm config 2). */
        baseband::run_image(portapack::spi_flash::image_tag_nfm_audio);
        receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
        receiver_model.set_nbfm_configuration(2);
        audio::set_rate(audio::Rate::Hz_24000);
        audio::output::start();
    }

    receiver_model.set_sampling_rate(3072000);
    receiver_model.set_baseband_bandwidth(1750000);
    receiver_model.enable();

    if (!digital_) {
        text_code.set("-");
        text_ids.set("-");
        text_frame.set("-");
        text_info.set("-");
        console.clear(true);
    }
}

void PMR446View::on_frame(const DPMRFrameMessage& message) {
    const std::string err = to_string_dec_uint(message.corrected) + "/" +
                            to_string_dec_uint(message.failed_blocks) +
                            (message.crc_ok ? " OK" : " BAD");

    if (message.frame_type == DPMRFrameMessage::HEADER) {
        std::string ci{"-"};
        if (message.header_type == 0 || message.header_type == 1)
            ci = to_string_dec_uint(message.ci_type) + "/" +
                 to_string_dec_uint(message.ci_info, 3, '0');

        std::string called{hex6(message.called_id)};
        const int32_t common = ::pmr446::isf_common_id(message.called_id);
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
    const int32_t common = ::pmr446::isf_common_id(message.called_id);
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

void PMR446View::on_tch(const DPMRTchDataMessage& message) {
    if (message.frame_number != 0)
        return; /* one dump per superframe keeps the console readable */

    console.write("TCH FN0:");
    for (size_t i = 0; i < message.length; i++)
        console.write(to_string_hex(message.data[i], 2));
    console.write("\n");
}

}  // namespace ui::external_app::pmr446
