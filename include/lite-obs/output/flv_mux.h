/******************************************************************************
    Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include "lite-obs/lite_encoder_info.h"

#define MILLISECOND_DEN 1000

static inline int32_t get_ms_time(const std::shared_ptr<encoder_packet> &packet, int64_t val)
{
    return (int32_t)(val * MILLISECOND_DEN / packet->timebase_den);
}

extern void flv_meta_data(int width, int height, int vb, int frame_rate,
                          int channels, int sample_rate, int ab,
                          std::vector<uint8_t> &output, bool write_header);
extern void flv_packet_mux(const std::shared_ptr<encoder_packet> &packet, int32_t dts_offset,
                           std::vector<uint8_t> &output, bool is_header);
