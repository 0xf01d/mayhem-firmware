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

#include "proc_dpmr444.hpp"

#include "event_m4.hpp"

#include <memory>

DPMRProcessor::DPMRProcessor() {
    decim_0.configure(taps_dpmr_decim_0.taps);
    decim_1.configure(taps_dpmr_decim_1.taps);
    channel_filter.configure(taps_dpmr_channel.taps, 1);
    demod.configure(channel_fs, 1050.0f); /* maximum deviation, table 12.1 */

    baseband_thread.start();
    configured = true;
}

void DPMRProcessor::execute(const buffer_c8_t& buffer) {
    if (!configured) return;

    const auto decim_0_out = decim_0.execute(buffer, dst_buffer_0);
    const auto decim_1_out = decim_1.execute(decim_0_out, dst_buffer_1);

    feed_channel_stats(decim_1_out);

    const auto filtered = channel_filter.execute(decim_1_out, dst_buffer_1);
    const auto audio_out = demod.execute(filtered, audio_buffer);

    for (size_t i = 0; i < audio_out.count; i++)
        engine.on_symbol(audio_out.p[i]);
}

void DPMRProcessor::on_message(const Message* const message) {
    (void)message; /* fixed 4FSK/2400 baud configuration, nothing to set */
}

int main() {
    EventDispatcher event_dispatcher{std::make_unique<DPMRProcessor>()};
    event_dispatcher.run();
    return 0;
}
