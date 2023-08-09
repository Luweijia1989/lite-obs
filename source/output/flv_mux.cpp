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

#include <stdio.h>
#include "lite-obs/output/flv_mux.h"
#include "librtmp/rtmp-helpers.h"
#include "lite-obs/util/serialize_op.h"

/* TODO: FIXME: this is currently hard-coded to h264 and aac!  ..not that we'll
 * use anything else for a long time. */

//#define DEBUG_TIMESTAMPS
//#define WRITE_FLV_HEADER

#define VIDEODATA_AVCVIDEOPACKET 7.0
#define AUDIODATA_AAC 10.0

static void build_flv_meta_data(int width, int height, int vb, int frame_rate,
                                int channels, int sample_rate, int ab, std::vector<uint8_t> &output)
{
    char buf[4096];
    char *enc = buf;
    char *end = enc + sizeof(buf);

    enc_str(&enc, end, "@setDataFrame");
    enc_str(&enc, end, "onMetaData");

    *enc++ = AMF_ECMA_ARRAY;
    enc = AMF_EncodeInt32(enc, end, 20);

    enc_num_val(&enc, end, "duration", 0.0);
    enc_num_val(&enc, end, "fileSize", 0.0);

    enc_num_val(&enc, end, "width", (double)width);
    enc_num_val(&enc, end, "height", (double)height);

    enc_num_val(&enc, end, "videocodecid", VIDEODATA_AVCVIDEOPACKET);
    enc_num_val(&enc, end, "videodatarate", vb);
    enc_num_val(&enc, end, "framerate", frame_rate);

    enc_num_val(&enc, end, "audiocodecid", AUDIODATA_AAC);
    enc_num_val(&enc, end, "audiodatarate", ab);
    enc_num_val(&enc, end, "audiosamplerate", (double)sample_rate);
    enc_num_val(&enc, end, "audiosamplesize", 16.0);
    enc_num_val(&enc, end, "audiochannels", (double)channels);

    enc_bool_val(&enc, end, "stereo", channels == 2);
    enc_bool_val(&enc, end, "2.1", channels == 3);
    enc_bool_val(&enc, end, "3.1", channels == 4);
    enc_bool_val(&enc, end, "4.0", channels == 4);
    enc_bool_val(&enc, end, "4.1", channels == 5);
    enc_bool_val(&enc, end, "5.1", channels == 6);
    enc_bool_val(&enc, end, "7.1", channels == 8);

    enc_str_val(&enc, end, "encoder", "lite obs rtmp output");

    *enc++ = 0;
    *enc++ = 0;
    *enc++ = AMF_OBJECT_END;

    output.resize(enc - buf);
    memcpy(output.data(), buf, enc - buf);
}

void flv_meta_data(int width, int height, int vb, int frame_rate,
                   int channels, int sample_rate, int ab,
                   std::vector<uint8_t> &output, bool write_header)
{
    std::vector<uint8_t> meta_data;
    build_flv_meta_data(width, height, vb, frame_rate, channels, sample_rate, ab, meta_data);

    serialize_op s(output);
    if (write_header) {
        s.s_write("FLV", 3);
        s.s_w8(1);
        s.s_w8(5);
        s.s_wb32(9);
        s.s_wb32(0);
    }

    auto start_pos = output.size();

    s.s_w8(RTMP_PACKET_TYPE_INFO);

    s.s_wb24((uint32_t)meta_data.size());
    s.s_wb32(0);
    s.s_wb24(0);

    s.s_write(meta_data.data(), meta_data.size());

    auto current_pos = output.size();
    s.s_wb32((uint32_t)current_pos - (uint32_t)start_pos - 1);
}

#ifdef DEBUG_TIMESTAMPS
static int32_t last_time = 0;
#endif

static void flv_video(std::vector<uint8_t> &output, int32_t dts_offset,
                      const std::shared_ptr<encoder_packet> &packet, bool is_header)
{
    int64_t offset = packet->pts - packet->dts;
    int32_t time_ms = get_ms_time(packet, packet->dts) - dts_offset;

    if (!packet->data || !packet->data->size())
        return;

    serialize_op s(output);
    s.s_w8(RTMP_PACKET_TYPE_VIDEO);

#ifdef DEBUG_TIMESTAMPS
    blog(LOG_DEBUG, "Video: %lu", time_ms);

    if (last_time > time_ms)
        blog(LOG_DEBUG, "Non-monotonic");

    last_time = time_ms;
#endif

    s.s_wb24((uint32_t)packet->data->size() + 5);
    s.s_wb24(time_ms);
    s.s_w8((time_ms >> 24) & 0x7F);
    s.s_wb24(0);

    /* these are the 5 extra bytes mentioned above */
    s.s_w8(packet->keyframe ? 0x17 : 0x27);
    s.s_w8(is_header ? 0 : 1);
    s.s_wb24(get_ms_time(packet, offset));
    s.s_write(packet->data->data(), packet->data->size());

    /* write tag size (starting byte doesn't count) */
    s.s_wb32((uint32_t)output.size() - 1);
}

static void flv_audio(std::vector<uint8_t> &output, int32_t dts_offset,
                      const std::shared_ptr<encoder_packet> &packet, bool is_header)
{
    int32_t time_ms = get_ms_time(packet, packet->dts) - dts_offset;

    if (!packet->data || !packet->data->size())
        return;

    serialize_op s(output);
    s.s_w8(RTMP_PACKET_TYPE_AUDIO);

#ifdef DEBUG_TIMESTAMPS
    blog(LOG_DEBUG, "Audio: %lu", time_ms);

    if (last_time > time_ms)
        blog(LOG_DEBUG, "Non-monotonic");

    last_time = time_ms;
#endif

    s.s_wb24((uint32_t)packet->data->size() + 2);
    s.s_wb24(time_ms);
    s.s_w8((time_ms >> 24) & 0x7F);
    s.s_wb24(0);

    /* these are the two extra bytes mentioned above */
    s.s_w8(0xaf);
    s.s_w8(is_header ? 0 : 1);
    s.s_write(packet->data->data(), packet->data->size());

    /* write tag size (starting byte doesn't count) */
    s.s_wb32((uint32_t)output.size() - 1);
}

void flv_packet_mux(const std::shared_ptr<encoder_packet> &packet, int32_t dts_offset,
                    std::vector<uint8_t> &output, bool is_header)
{
    if (packet->type == obs_encoder_type::OBS_ENCODER_VIDEO)
        flv_video(output, dts_offset, packet, is_header);
    else
        flv_audio(output, dts_offset, packet, is_header);
}
