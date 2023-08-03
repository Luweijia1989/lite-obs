#include "lite-obs/media-io/video-matrices.h"
#include <memory>

static struct {
    video_colorspace const color_space;
	float const Kb, Kr;
	int const range_min[3];
	int const range_max[3];
	int const black_levels[2][3];

	float float_range_min[3];
	float float_range_max[3];
	float matrix[2][16];

} format_info[] = {
    {video_colorspace::VIDEO_CS_601,
	 0.114f,
	 0.299f,
	 {16, 16, 16},
	 {235, 240, 240},
	 {{16, 128, 128}, {0, 128, 128}},
#ifndef COMPUTE_MATRICES
	 {16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f},
	 {235.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f},
	 {{1.164384f, 0.000000f, 1.596027f, -0.874202f, 1.164384f, -0.391762f,
	   -0.812968f, 0.531668f, 1.164384f, 2.017232f, 0.000000f, -1.085631f,
	   0.000000f, 0.000000f, 0.000000f, 1.000000f},
	  {1.000000f, 0.000000f, 1.407520f, -0.706520f, 1.000000f, -0.345491f,
	   -0.716948f, 0.533303f, 1.000000f, 1.778976f, 0.000000f, -0.892976f,
	   0.000000f, 0.000000f, 0.000000f, 1.000000f}}
#endif
	},
    {video_colorspace::VIDEO_CS_709,
	 0.0722f,
	 0.2126f,
	 {16, 16, 16},
	 {235, 240, 240},
	 {{16, 128, 128}, {0, 128, 128}},
#ifndef COMPUTE_MATRICES
	 {16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f},
	 {235.0f / 255.0f, 240.0f / 255.0f, 240.0f / 255.0f},
	 {{1.164384f, 0.000000f, 1.792741f, -0.972945f, 1.164384f, -0.213249f,
	   -0.532909f, 0.301483f, 1.164384f, 2.112402f, 0.000000f, -1.133402f,
	   0.000000f, 0.000000f, 0.000000f, 1.000000f},
	  {1.000000f, 0.000000f, 1.581000f, -0.793600f, 1.000000f, -0.188062f,
	   -0.469967f, 0.330305f, 1.000000f, 1.862906f, 0.000000f, -0.935106f,
	   0.000000f, 0.000000f, 0.000000f, 1.000000f}}
#endif
	},
};

#define NUM_FORMATS (sizeof(format_info) / sizeof(format_info[0]))

static const float full_min[3] = {0.0f, 0.0f, 0.0f};
static const float full_max[3] = {1.0f, 1.0f, 1.0f};

bool video_format_get_parameters(video_colorspace color_space,
                 video_range_type range, float matrix[16],
				 float range_min[3], float range_max[3])
{
    if (color_space == video_colorspace::VIDEO_CS_DEFAULT) {
        color_space = video_colorspace::VIDEO_CS_601;
    }

	for (size_t i = 0; i < NUM_FORMATS; i++) {
		if (format_info[i].color_space != color_space)
			continue;

        int full_range = range == video_range_type::VIDEO_RANGE_FULL ? 1 : 0;
		memcpy(matrix, format_info[i].matrix[full_range],
		       sizeof(float) * 16);

        if (range == video_range_type::VIDEO_RANGE_FULL) {
			if (range_min)
				memcpy(range_min, full_min, sizeof(float) * 3);
			if (range_max)
				memcpy(range_max, full_max, sizeof(float) * 3);
			return true;
		}

		if (range_min)
			memcpy(range_min, format_info[i].float_range_min,
			       sizeof(float) * 3);

		if (range_max)
			memcpy(range_max, format_info[i].float_range_max,
			       sizeof(float) * 3);

		return true;
	}
	return false;
}
