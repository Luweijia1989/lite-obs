#pragma once

#include "gs_texture.h"

struct gs_texture_render_private;
class gs_texture_render
{
public:
    gs_texture_render(gs_color_format format, gs_zstencil_format zsformat);
    ~gs_texture_render();

    bool gs_texrender_resetbuffer(uint32_t cx, uint32_t cy);
    bool gs_texrender_begin(uint32_t cx, uint32_t cy);
    void gs_texrender_end();
    void gs_texrender_reset();
    std::shared_ptr<gs_texture> gs_texrender_get_texture();

private:
    std::shared_ptr<gs_texture_render_private> d_ptr{};
};
