#pragma once

#include <memory>
#include "video_info.h"

#define VIDEO_SCALER_SUCCESS 0
#define VIDEO_SCALER_BAD_CONVERSION -1
#define VIDEO_SCALER_FAILED -2

struct video_scale_info {
    video_format format = video_format::VIDEO_FORMAT_NONE;
    uint32_t width{};
    uint32_t height{};
    video_range_type range = video_range_type::VIDEO_RANGE_DEFAULT;
    video_colorspace colorspace = video_colorspace::VIDEO_CS_DEFAULT;
};

struct video_scaler_private;
class video_scaler
{
public:
    video_scaler();
    ~video_scaler();

    int create(const struct video_scale_info *dst, const struct video_scale_info *src, video_scale_type type);
    bool video_scaler_scale(uint8_t *output[], uint32_t out_linesize[], const uint8_t *const input[], const uint32_t in_linesize[]);

private:
    std::unique_ptr<video_scaler_private> d_ptr{};
};
