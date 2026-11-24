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
#include "rtc_time.hpp"
#include "string_format.hpp"

using namespace portapack;
using namespace std::literals;

namespace ui::external_app::pmr446 {

namespace {

std::vector<OptionsField::option_t> channel_options() {
    std::vector<OptionsField::option_t> options;
    for (size_t ch = 1; ch <= ::pmr446::analog_channel_count; ch++)
        options.emplace_back(to_string_dec_uint(ch), static_cast<int32_t>(ch));
    return options;
}

/* ECC/DEC/(15)05 listen-before-transmit: a candidate channel must be below
 * -105 dBm for 100 ms. The ChannelStatistics reports channel power relative
 * to full scale (0 dB = ADC full scale), and -105 dBm is about 27 dB above
 * this receiver's noise floor, which maps to this value on that scale. */
constexpr int32_t lbt_threshold_db = -67;

/* ChannelStatistics arrives every 100 ms (RSSIStatisticsCollector), which is
 * exactly the LBT window. The channel must be quiet for one full window. */
constexpr uint8_t lbt_windows_quiet = 1;

}  // namespace

void PMR446View::focus() {
    options_direction.focus();
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
        &options_direction,
        &options_channel,
        &options_polite,
        &text_status,
        &text_countdown,
        &console,
        &tx_view,
        &button_ptt,
    });

    console.enable_scrolling(true);

    options_direction.on_change = [this](size_t, int32_t value) {
        if (transmitting_ || lbt_pending_)
            end_transmit();
        direction_ = static_cast<uint32_t>(value);
        apply_direction();
    };

    options_channel.on_change = [this](size_t, int32_t value) {
        if (transmitting_ || lbt_pending_)
            end_transmit();
        channel_ = static_cast<uint32_t>(value);
        set_channel_frequency();
    };

    options_polite.on_change = [this](size_t, int32_t value) {
        polite_ = static_cast<uint32_t>(value);
    };

    tx_view.on_bandwidth_changed = [this]() {
        /* Fixed 12.5 kHz PMR446 channel; force it back. */
        transmitter_model.set_channel_bandwidth(12500);
    };

    tx_view.on_start = [this]() {
        start_lbt();
    };

    tx_view.on_stop = [this]() {
        end_transmit();
    };

    button_ptt.on_select = [this](Button&) {
        if (transmitting_ || lbt_pending_)
            end_transmit();
        else
            start_lbt();
    };

    options_direction.set_by_value(direction_);
    options_channel.set_options(channel_options());
    if (channel_ < 1 || channel_ > ::pmr446::analog_channel_count)
        channel_ = 1;
    options_channel.set_by_value(channel_);
    options_polite.set_by_value(polite_);
    set_channel_frequency();

    signal_token_tick_second = rtc_time::signal_tick_second += [this]() {
        on_tick_second();
    };

    apply_direction();
}

PMR446View::~PMR446View() {
    rtc_time::signal_tick_second -= signal_token_tick_second;
    transmitting_ = false;
    lbt_pending_ = false;
    countdown_active_ = false;
    transmitter_model.disable();
    audio::input::stop();
    receiver_model.disable();
    baseband::shutdown();
    audio::output::stop();
}

void PMR446View::set_channel_frequency() {
    const uint32_t frequency = ::pmr446::analog_frequency_hz(channel_);
    field_frequency.set_value(frequency);
    if (direction_ == 0)
        receiver_model.set_target_frequency(frequency);
    else
        transmitter_model.set_target_frequency(frequency);
}

void PMR446View::apply_direction() {
    audio::output::stop();
    receiver_model.disable();
    transmitter_model.disable();
    baseband::shutdown();

    set_channel_frequency();

    if (direction_ == 0) {
        /* Analogue PMR446: 12.5 kHz NFM voice, stock image. */
        baseband::run_image(portapack::spi_flash::image_tag_nfm_audio);
        receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
        receiver_model.set_nbfm_configuration(2);
        receiver_model.set_squelch_level(0);
        audio::set_rate(audio::Rate::Hz_24000);
        audio::output::start();
        receiver_model.enable();
        text_status.set("RX ch" + to_string_dec_uint(channel_));
        text_countdown.set("-");
    } else {
        /* Analogue PMR446 TX: stock mic_tx image, armed on PTT. */
        text_status.set("TX ch" + to_string_dec_uint(channel_) + " standby");
        text_countdown.set("Idle");
    }
    console.clear(true);
}

void PMR446View::start_lbt() {
    if (direction_ != 1 || transmitting_ || lbt_pending_)
        return;

    if (!polite_) {
        begin_transmit();
        return;
    }

    /* Listen before transmit: tune RX to the candidate channel, watch it for
     * one 100 ms statistics window, then transmit if it was quiet. */
    receiver_model.disable();
    baseband::shutdown();
    audio::output::stop();
    baseband::run_image(portapack::spi_flash::image_tag_nfm_audio);
    receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
    receiver_model.set_nbfm_configuration(2);
    receiver_model.set_squelch_level(0);
    receiver_model.set_target_frequency(::pmr446::analog_frequency_hz(channel_));
    receiver_model.enable();

    lbt_peak_db_ = -120;
    lbt_quiet_windows_ = 0;
    lbt_pending_ = true;
    text_status.set("LBT ch" + to_string_dec_uint(channel_));
    text_countdown.set("Checking");
}

void PMR446View::begin_transmit() {
    lbt_pending_ = false;
    receiver_model.disable();
    baseband::shutdown();

    audio::set_rate(audio::Rate::Hz_24000);
    audio::input::start(0, false);
    baseband::run_image(portapack::spi_flash::image_tag_mic_tx);
    baseband::set_audiotx_config(
        1536000 / 20,
        3000,
        1.0f,
        4,
        8,
        0,
        false, false, false, false);
    transmitter_model.set_target_frequency(::pmr446::analog_frequency_hz(channel_));
    transmitter_model.set_baseband_bandwidth(1'750'000);
    transmitter_model.enable();

    transmitting_ = true;
    tx_seconds_left_ = max_tx_seconds_;
    countdown_active_ = true;
    tx_view.set_transmitting(true);
    text_status.set("TX ch" + to_string_dec_uint(channel_));
    text_countdown.set("TX " + to_string_dec_uint(tx_seconds_left_) + "s");
    console.write("TX start ch" + to_string_dec_uint(channel_) + "\n");
}

void PMR446View::end_transmit() {
    const bool was_active = transmitting_ || lbt_pending_;
    transmitting_ = false;
    lbt_pending_ = false;
    countdown_active_ = false;

    transmitter_model.disable();
    audio::input::stop();
    baseband::shutdown();
    tx_view.set_transmitting(false);

    if (was_active) {
        console.write("TX stop\n");
        /* Back to standby RX on the selected channel. */
        apply_direction();
    }
}

void PMR446View::on_statistics_update(const ChannelStatistics& statistics) {
    if (!lbt_pending_)
        return;

    if (statistics.max_db > lbt_peak_db_)
        lbt_peak_db_ = statistics.max_db;

    /* One statistics message is one 100 ms window. */
    if (lbt_peak_db_ < lbt_threshold_db) {
        if (++lbt_quiet_windows_ >= lbt_windows_quiet)
            begin_transmit();
    } else {
        /* Busy: restart the window and keep listening. */
        lbt_quiet_windows_ = 0;
        lbt_peak_db_ = -120;
        text_countdown.set("Busy");
    }
}

void PMR446View::on_tick_second() {
    if (!transmitting_ || !countdown_active_)
        return;

    if (tx_seconds_left_ > 0) {
        tx_seconds_left_--;
        text_countdown.set("TX " + to_string_dec_uint(tx_seconds_left_) + "s");
        if (tx_seconds_left_ == 0) {
            console.write("TX time limit (ECC/DEC/(15)05)\n");
            end_transmit();
        }
    }
}

}  // namespace ui::external_app::pmr446
