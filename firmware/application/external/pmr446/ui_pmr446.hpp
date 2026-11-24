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

#include "app_settings.hpp"
#include "pmr446_channels.hpp"
#include "radio_state.hpp"
#include "ui.hpp"
#include "ui_freq_field.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "theme.hpp"

#include <string>
#include <vector>

using namespace ui;

namespace ui::external_app::pmr446 {

/* PMR446 / dPMR446 receiver: analogue FM listen (16ch, 12.5 kHz) and
 * digital dPMR446 layer-2 decode (32ch, 6.25 kHz). RX only, no vocoder. */
class PMR446View : public View {
   public:
    PMR446View(NavigationView& nav);
    ~PMR446View();

    void focus() override;

    std::string title() const override { return "PMR446"; }

   private:
    NavigationView& nav_;

    uint32_t digital_{1}; /* 0 = analogue FM, 1 = dPMR446 */
    uint32_t channel_{1};

    app_settings::SettingsManager settings_{
        "rx_pmr446",
        app_settings::Mode::RX,
        {
            {"digital"sv, &digital_},
            {"channel"sv, &channel_},
        }};

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
        {{UI_POS_X(0), UI_POS_Y(1)}, "Mode:       Ch:        CC:",
         Theme::getInstance()->fg_light->foreground}};

    OptionsField options_mode{
        {UI_POS_X(6), UI_POS_Y(1)},
        4,
        {{"FM", 0},
         {"dPMR", 1}}};

    OptionsField options_channel{
        {UI_POS_X(15), UI_POS_Y(1)},
        4,
        {}};

    Text text_code{
        {UI_POS_X(23), UI_POS_Y(1), UI_POS_WIDTH_REMAINING(24), 16},
        "-"};

    Text text_ids{
        {UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, 16},
        "-"};
    Text text_frame{
        {UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, 16},
        "-"};
    Text text_info{
        {UI_POS_X(0), UI_POS_Y(4), UI_POS_MAXWIDTH, 16},
        "-"};

    Console console{
        {UI_POS_X(0), UI_POS_Y(5), UI_POS_MAXWIDTH, UI_POS_HEIGHT_REMAINING(1)}};

    void apply_mode();
    void rebuild_channel_options();
    void set_channel_frequency();
    void on_frame(const DPMRFrameMessage& message);
    void on_tch(const DPMRTchDataMessage& message);

    MessageHandlerRegistration message_handler_frame{
        Message::ID::DPMRFrame,
        [this](Message* const p) {
            this->on_frame(*static_cast<const DPMRFrameMessage*>(p));
        }};

    MessageHandlerRegistration message_handler_tch{
        Message::ID::DPMRTchData,
        [this](Message* const p) {
            this->on_tch(*static_cast<const DPMRTchDataMessage*>(p));
        }};
};

}  // namespace ui::external_app::pmr446

#endif /*__UI_PMR446_H__*/
