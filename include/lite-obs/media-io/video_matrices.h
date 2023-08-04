#pragma once

#include "video_info.h"

bool video_format_get_parameters(video_colorspace color_space,
                 video_range_type range, float matrix[16],
                 float range_min[3], float range_max[3]);
