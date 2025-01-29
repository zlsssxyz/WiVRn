/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "bitrate_control.h"

#include "encoder/video_encoder.h"
#include "encoder_settings.h"

#include "util/u_logging.h"
#include <algorithm>
#include <cassert>
#include <cmath>

wivrn::bitrate_control::bitrate_control(
        int64_t frame_time,
        const std::vector<std::shared_ptr<video_encoder>> & encoders,
        const std::vector<encoder_settings> & settings,
        double target_occupancy) :
        frame_time(frame_time),
        target_occupancy(target_occupancy)
{
	assert(encoders.size() == settings.size());
	for (const auto & s: settings)
		current_bitrate += s.bitrate;

	for (size_t i = 0; i < encoders.size(); ++i)
	{
		this->encoders.emplace_back(
		        encoders[i],
		        double(settings[i].bitrate) / current_bitrate,
		        settings[i].channels == to_headset::video_stream_description::channels_t::alpha);
	}
}

void wivrn::bitrate_control::on_feedback(const from_headset::feedback & feedback)
{
	if (feedback.frame_index < frame_index_backoff)
		return;
	if (feedback.stream_index >= encoders.size())
		return;
	if (not feedback.received_from_decoder)
		return;

	auto & it = timings[feedback.frame_index % timings.size()];
	if (it.frame_id > feedback.frame_index)
		return;
	if (it.frame_id < feedback.frame_index)
	{
		it = {
		        .encode_begin = feedback.encode_begin,
		        .encode_end = feedback.encode_end,
		        .network_begin = std::min(feedback.send_begin, feedback.received_first_packet),
		        .network_end = std::max(feedback.send_end, feedback.received_last_packet),
		        .decode_begin = feedback.sent_to_decoder,
		        .decode_end = feedback.received_from_decoder,
		        .num_encoders = 1,
		        .frame_id = feedback.frame_index,
		};
	}
	else
	{
		it.network_begin = std::min(it.network_begin, std::min(feedback.send_begin, feedback.received_first_packet));
		it.network_end = std::min(it.network_end, std::max(feedback.send_end, feedback.received_last_packet));
		it.decode_begin = std::min(it.decode_begin, feedback.sent_to_decoder);
		it.decode_end = std::min(it.decode_end, feedback.received_from_decoder);
		++it.num_encoders;
	}

	if (it.num_encoders == std::ranges::count(encoders, alpha.load(), [](const auto & i) { return i.alpha; }))
	{
		int64_t t = std::max({it.encode_end - it.encode_begin,
		                      it.network_end - it.network_begin,
		                      it.decode_end - it.decode_begin});
		average_time = average_time ? std::lerp(average_time, t, 0.1) : t;
		++frame_samples;

		U_LOG_W("encode %ld network %ld decode %ld",
		        (it.encode_end - it.encode_begin) / 1'000,
		        (it.network_end - it.network_begin) / 1'000,
		        (it.decode_end - it.decode_begin) / 1'000);

		if (frame_samples > 200)
		{
			double change = 1.0;
			U_LOG_W("target %ld - %ld, average %ld",
			        int64_t(frame_time * (target_occupancy - 0.1)) / 1'000,
			        int64_t(frame_time * (target_occupancy + 0.1)) / 1'000,
			        average_time / 1'000);
			if (average_time > frame_time * (target_occupancy + 0.1))
				change = 0.9;
			else if (average_time < frame_time * (target_occupancy - 0.1))
				change = 1.1;

			if (change != 1.0)
			{
				current_bitrate *= change;
				for (auto & i: encoders)
					i.encoder->set_bitrate(current_bitrate * i.bitrate_fraction);

				frame_index_backoff = it.frame_id + 300;
				frame_samples = 0;
				average_time = 0;
			}
		}
	}
}
