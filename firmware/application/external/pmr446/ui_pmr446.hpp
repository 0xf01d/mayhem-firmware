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

#ifndef __UI_PMR446_H__
#define __UI_PMR446_H__

/* Analogue PMR446 (16 channels, 12.5 kHz NFM, ECC/DEC/(15)05):
 * plain NFM voice, RX through the stock nfm_audio image and TX through the
 * stock mic_tx image. No custom baseband, so this app cannot be affected by
 * baseband image size limits. TX asks for a free channel (RSSI_LO -105 dBm,
 * 100 ms) and is capped at 180 s per ECC/DEC/(15)05. */

#include "app_settings.hpp"
#include "pmr446_channels.hpp"
#include "radio_state.hpp"
#include "rtc_time.hpp"
#include "signal.hpp"
#include "ui.hpp"
#include "ui_freq_field.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_textentry.hpp"
#include "ui_transmitter.hpp"
#include "theme.hpp"

#include <string>

using namespace ui;

namespace ui::external_app::pmr446 {

class PMR446View : public View {
   public:
    PMR446View(NavigationView& nav);
    ~PMR446View();

    void focus() override;

    std::string title() const override { return "PMR446"; }

   private:
    NavigationView& nav_;

    /* 0 = RX, 1 = TX. */
    uint32_t direction_{0};

    uint32_t channel_{1};

    /* polite: wait for a free channel; impolite: transmit regardless. */
    uint32_t polite_{1};

    bool transmitting_{false};
    bool lbt_pending_{false};

    uint32_t max_tx_seconds_{180};
    uint32_t tx_seconds_left_{0};
    bool countdown_active_{false};

    /* Highest channel power seen since the LBT window opened, dBFS. */
    int32_t lbt_peak_db_{-120};
    uint8_t lbt_quiet_windows_{0};

    app_settings::SettingsManager settings_{
        "pmr446",
        app_settings::Mode::RX_TX,
        {
            {"direction"sv, &direction_},
            {"channel"sv, &channel_},
            {"polite"sv, &polite_},
        }};

    RxRadioState rx_radio_state_{};
    TxRadioState tx_radio_state_{
        446006250 /* frequency */,
        12500 /* bandwidth */,
        1536000 /* sampling rate */};

    RxFrequencyField field_frequency{
        {UI_POS_X(0), UI_POS_Y(0)},
        nav_};

    RFAmpField field_rf_amp{
        {UI_POS_X(13), UI_POS_Y(0)}};
    LNAGainField field_lna{
        {UI_POS_X(15), UI_POS_Y(0)}};
    VGAGainField field_vga{
        {UI_POS_X(18), UI_POS_Y(0)}};
    RSSI rssi{
        {UI_POS_X(21), UI_POS_Y(0), UI_POS_WIDTH_REMAINING(24), 4}};
    AudioVolumeField field_volume{{UI_POS_X_RIGHT(2), UI_POS_Y(0)}};

    Labels labels{
        {{UI_POS_X(0), UI_POS_Y(1)}, "Dir:    Ch:     Mode:", Theme::getInstance()->fg_light->foreground}};

    OptionsField options_direction{
        {UI_POS_X(5), UI_POS_Y(1)},
        3,
        {{"RX", 0}, {"TX", 1}}};

    OptionsField options_channel{
        {UI_POS_X(13), UI_POS_Y(1)},
        3,
        {}};

    OptionsField options_polite{
        {UI_POS_X(21), UI_POS_Y(1)},
        7,
        {{"POLITE", 0}, {"IMPOL", 1}}};

    Text text_status{
        {UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, 16},
        "-"};

    Text text_countdown{
        {UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, 16},
        "-"};

    Console console{
        {UI_POS_X(0), UI_POS_Y(5), UI_POS_MAXWIDTH, UI_POS_HEIGHT_REMAINING(1)}};

    TransmitterView tx_view{
        UI_POS_Y_BOTTOM(7),
        12500,
        12500,
        false};

    Button button_ptt{
        {UI_POS_X_CENTER(12), UI_POS_Y_BOTTOM(5), UI_POS_WIDTH(12), UI_POS_HEIGHT(2)},
        "PTT"};

    void apply_direction();
    void rebuild_channel_options();
    void set_channel_frequency();
    void start_lbt();
    void begin_transmit();
    void end_transmit();
    void update_tx_state();
    void on_statistics_update(const ChannelStatistics& statistics);
    void on_tick_second();

    SignalToken signal_token_tick_second{};

    MessageHandlerRegistration message_handler_stats{
        Message::ID::ChannelStatistics,
        [this](Message* const p) {
            this->on_statistics_update(static_cast<const ChannelStatisticsMessage*>(p)->statistics);
        }};
};

}  // namespace ui::external_app::pmr446

#endif /*__UI_PMR446_H__*/
