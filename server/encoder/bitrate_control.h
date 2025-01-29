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

#pragma once

#include <atomic>
#include <memory>
#include <vector>

namespace wivrn
{
struct encoder_settings;
class video_encoder;
namespace from_headset
{
struct feedback;
};

class bitrate_control
{
	struct item
	{
		std::shared_ptr<video_encoder> encoder;
		double bitrate_fraction;
		bool alpha;
	};
	struct timings_t
	{
		int64_t encode_begin;
		int64_t encode_end;
		int64_t network_begin;
		int64_t network_end;
		int64_t decode_begin;
		int64_t decode_end;
		uint8_t num_encoders;
		uint64_t frame_id;
	};
	std::vector<item> encoders;
	std::array<timings_t, 2> timings{};
	int64_t frame_time;
	uint64_t current_bitrate = 0;
	int64_t average_time = 0;
	uint64_t frame_samples = 0;
	std::atomic<bool> alpha = false;
	uint64_t frame_index_backoff = 300;
	double target_occupancy; // fraction of frame_time the longest step (encode, receive, decode) can take

public:
	bitrate_control(
	        int64_t frame_time,
	        const std::vector<std::shared_ptr<video_encoder>> & encoders,
	        const std::vector<encoder_settings> &,
	        double target_occupancy);
	void on_feedback(const from_headset::feedback &);
	void set_alpha(bool alpha)
	{
		this->alpha = alpha;
	}
};
} // namespace wivrn
