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

#ifndef __UI_DPMR446_H__
#define __UI_DPMR446_H__

/* dPMR446 (ETSI TS 102 490 V1.9.1): 32 channels of 6.25 kHz 4FSK.
 *
 * The digital half of the old combined PMR446 app, split out so the analogue
 * side can use only the stock images and can never be affected by the voice
 * image's size. This app runs the signalling/data receiver (image_tag_dpmr):
 * layer-2 header, CCH and end frames on the console.
 *
 * Voice RX and TX need the AMBE+2 vocoder images. Those do not currently fit
 * the 32752 B baseband cap (see the lane report); this app only bundles the
 * signalling image, so it is always safe. */

#include "app_settings.hpp"
#include "dpmr446_channels.hpp"
#include "radio_state.hpp"
#include "ui.hpp"
#include "ui_freq_field.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "theme.hpp"

#include <string>

using namespace ui;

namespace ui::external_app::dpmr446 {

class DPMR446View : public View {
   public:
    DPMR446View(NavigationView& nav);
    ~DPMR446View();

    void focus() override;

    std::string title() const override { return "dPMR446"; }

   private:
    NavigationView& nav_;

    uint32_t channel_{1};

    app_settings::SettingsManager settings_{
        "dpmr446",
        app_settings::Mode::RX,
        {
            {"channel"sv, &channel_},
        }};

    RxRadioState radio_state_{};

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
        {{UI_POS_X(0), UI_POS_Y(1)}, "Ch:        CC:", Theme::getInstance()->fg_light->foreground}};

    OptionsField options_channel{
        {UI_POS_X(3), UI_POS_Y(1)},
        3,
        {}};

    Text text_code{
        {UI_POS_X(14), UI_POS_Y(1), UI_POS_WIDTH_REMAINING(14), 16},
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

    void rebuild_channel_options();
    void set_channel_frequency();
    void apply_mode();

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

}  // namespace ui::external_app::dpmr446

#endif /*__UI_DPMR446_H__*/
