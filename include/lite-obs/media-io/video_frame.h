#pragma once

#include "lite-obs/lite_obs_defines.h"
#include <stdint.h>
#include <memory>
#include <vector>

void video_frame_init(std::vector<uint8_t> &buffer, std::vector<uint8_t *> &data, std::vector<uint32_t> &linesize, video_format format, uint32_t width, uint32_t height);

class video_frame
{
public:
    video_frame();
    ~video_frame();

    void frame_init(video_format format, uint32_t width, uint32_t height);
    void frame_free();

    static void video_frame_copy(video_frame *dst, const video_frame *src, video_format format, uint32_t cy);

    std::vector<uint8_t *> data{};
    std::vector<uint32_t> linesize{};

private:
    std::vector<uint8_t> data_internal{};
};
