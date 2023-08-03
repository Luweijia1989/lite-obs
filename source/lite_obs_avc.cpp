#include "lite-obs/lite_obs_avc.h"
#include "lite-obs/lite_encoder_info.h"
#include "lite-obs/util/serialize_op.h"

bool obs_avc_keyframe(const uint8_t *data, size_t size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data + size;
	int type;

	nal_start = obs_avc_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		type = nal_start[0] & 0x1F;

		if (type == OBS_NAL_SLICE_IDR || type == OBS_NAL_SLICE)
			return (type == OBS_NAL_SLICE_IDR);

		nal_end = obs_avc_find_startcode(nal_start, end);
		nal_start = nal_end;
	}

	return false;
}

/* NOTE: I noticed that FFmpeg does some unusual special handling of certain
 * scenarios that I was unaware of, so instead of just searching for {0, 0, 1}
 * we'll just use the code from FFmpeg - http://www.ffmpeg.org/ */
static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p,
						     const uint8_t *end)
{
	const uint8_t *a = p + 4 - ((intptr_t)p & 3);

	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t *)p;

		if ((x - 0x01010101) & (~x) & 0x80808080) {
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p + 1;
			}

			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p + 2;
				if (p[4] == 0 && p[5] == 1)
					return p + 3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}

const uint8_t *obs_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
	const uint8_t *out = ff_avc_find_startcode_internal(p, end);
	if (p < out && out < end && !out[-1])
		out--;
	return out;
}

static inline int get_drop_priority(int priority)
{
	return priority;
}

static void serialize_avc_data(serialize_op &op, const uint8_t *data, size_t size, bool *is_keyframe, int *priority)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data + size;
	int type;

	nal_start = obs_avc_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		type = nal_start[0] & 0x1F;

		if (type == OBS_NAL_SLICE_IDR || type == OBS_NAL_SLICE) {
			if (is_keyframe)
				*is_keyframe = (type == OBS_NAL_SLICE_IDR);
			if (priority)
				*priority = nal_start[0] >> 5;
		}

		nal_end = obs_avc_find_startcode(nal_start, end);
        op.s_wb32((uint32_t)(nal_end - nal_start));
        op.s_write(nal_start, nal_end - nal_start);
		nal_start = nal_end;
	}
}

std::shared_ptr<struct encoder_packet> obs_parse_avc_packet(std::shared_ptr<encoder_packet> src)
{
    std::shared_ptr<encoder_packet> avc_packet = std::make_shared<encoder_packet>();
	*avc_packet = *src;
    avc_packet->data = std::make_shared<std::vector<uint8_t>>();

    serialize_op op(*avc_packet->data.get());
    serialize_avc_data(op, src->data->data(), src->data->size(), &avc_packet->keyframe,
			   &avc_packet->priority);

	avc_packet->drop_priority = get_drop_priority(avc_packet->priority);
    return avc_packet;
}

static inline bool has_start_code(const uint8_t *data)
{
	if (data[0] != 0 || data[1] != 0)
		return false;

	return data[2] == 1 || (data[2] == 0 && data[3] == 1);
}

static void get_sps_pps(const uint8_t *data, size_t size, const uint8_t **sps,
			size_t *sps_size, const uint8_t **pps, size_t *pps_size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data + size;
	int type;

	nal_start = obs_avc_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		nal_end = obs_avc_find_startcode(nal_start, end);

		type = nal_start[0] & 0x1F;
		if (type == OBS_NAL_SPS) {
			*sps = nal_start;
			*sps_size = nal_end - nal_start;
		} else if (type == OBS_NAL_PPS) {
			*pps = nal_start;
			*pps_size = nal_end - nal_start;
		}

		nal_start = nal_end;
	}
}

void obs_parse_avc_header(std::vector<uint8_t> &header, const uint8_t *data, size_t size)
{
	const uint8_t *sps = NULL, *pps = NULL;
	size_t sps_size = 0, pps_size = 0;

    if (size <= 6)
        return;

    serialize_op op(header);
	if (!has_start_code(data)) {
        op.s_write(data, size);
        return;
	}

	get_sps_pps(data, size, &sps, &sps_size, &pps, &pps_size);
	if (!sps || !pps || sps_size < 4)
        return;

    op.s_w8(0x01);
    op.s_write(sps + 1, 3);
    op.s_w8(0xff);
    op.s_w8(0xe1);

    op.s_wb16((uint16_t)sps_size);
    op.s_write(sps, sps_size);
    op.s_w8(0x01);
    op.s_wb16((uint16_t)pps_size);
    op.s_write(pps, pps_size);
}

void obs_extract_avc_headers(const uint8_t *packet, size_t size,
                 std::shared_ptr<std::vector<uint8_t> > new_packet_data,
                 std::vector<uint8_t> &header_data,
                 std::vector<uint8_t> &sei_data)
{
	const uint8_t *nal_start, *nal_end, *nal_codestart;
	const uint8_t *end = packet + size;
	int type;

	nal_start = obs_avc_find_startcode(packet, end);
	nal_end = NULL;
	while (nal_end != end) {
		nal_codestart = nal_start;

		while (nal_start < end && !*(nal_start++))
			;

		if (nal_start == end)
			break;

		type = nal_start[0] & 0x1F;

		nal_end = obs_avc_find_startcode(nal_start, end);
		if (!nal_end)
			nal_end = end;

		if (type == OBS_NAL_SPS || type == OBS_NAL_PPS) {
            serialize_op op(header_data);
            op.s_write(nal_codestart, nal_end - nal_codestart);
		} else if (type == OBS_NAL_SEI) {
            serialize_op op(sei_data);
            op.s_write(nal_codestart, nal_end - nal_codestart);
		} else {
            serialize_op op(*new_packet_data.get());
            op.s_write(nal_codestart, nal_end - nal_codestart);
        }

		nal_start = nal_end;
	}
}
