#pragma once

#include <memory>
#include "gs_subsystem_info.h"

struct gs_stagesurface_private;
class gs_texture;
class gs_stagesurface
{
public:
    gs_stagesurface();
    ~gs_stagesurface();

    bool gs_stagesurface_create(uint32_t width, uint32_t height, gs_color_format color_format);

    void gs_stagesurface_stage_texture(std::shared_ptr<gs_texture> src);

    uint32_t gs_stagesurface_get_width();
    uint32_t gs_stagesurface_get_height();
    gs_color_format gs_stagesurface_get_color_format();
    bool gs_stagesurface_map(uint8_t **data, uint32_t *linesize);
    void gs_stagesurface_unmap();

private:
    bool create_pixel_pack_buffer();
    bool can_stage(std::shared_ptr<gs_texture> src);

private:
    std::unique_ptr<gs_stagesurface_private> d_ptr{};
};

std::shared_ptr<gs_stagesurface> gs_stagesurface_create(uint32_t width, uint32_t height, gs_color_format color_format);
