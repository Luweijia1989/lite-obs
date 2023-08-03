#include "lite-obs/graphics/gs_texture_render.h"
#include "lite-obs/graphics/gs_subsystem.h"

struct gs_texture_render_private
{
    std::shared_ptr<gs_texture> target{};
    std::weak_ptr<gs_texture> prev_target{};
    std::shared_ptr<gs_zstencil_buffer> zs{};
    std::weak_ptr<gs_zstencil_buffer> prev_zs{};

    uint32_t cx{}, cy{};

    gs_color_format format{};
    gs_zstencil_format zsformat{};

    bool rendered{};
};

gs_texture_render::gs_texture_render(gs_color_format format, gs_zstencil_format zsformat)
{
    d_ptr = std::make_unique<gs_texture_render_private>();
    d_ptr->format = format;
    d_ptr->zsformat = zsformat;
}

gs_texture_render::~gs_texture_render()
{
    blog(LOG_DEBUG, "gs_texture_render destroyed.");
}

bool gs_texture_render::gs_texrender_resetbuffer(uint32_t cx, uint32_t cy)
{
    d_ptr->target.reset();
    d_ptr->zs.reset();

    d_ptr->cx = cx;
    d_ptr->cy = cy;

    d_ptr->target = gs_texture_create(cx, cy, d_ptr->format, NULL, GS_RENDER_TARGET);
    if (!d_ptr->target)
        return false;

    if (d_ptr->zsformat != gs_zstencil_format::GS_ZS_NONE) {
        d_ptr->zs = std::make_shared<gs_zstencil_buffer>(cx, cy, d_ptr->zsformat);
        if (!d_ptr->zs) {
            d_ptr->target.reset();
            return false;
        }
    }

    return true;
}

bool gs_texture_render::gs_texrender_begin(uint32_t cx, uint32_t cy)
{
    if (d_ptr->rendered)
        return false;

    if (!cx || !cy)
        return false;

    if (d_ptr->cx != cx || d_ptr->cy != cy)
        if (!gs_texrender_resetbuffer(cx, cy))
            return false;

    if (!d_ptr->target)
        return false;

    gs_viewport_push();
    gs_projection_push();
    gs_matrix_push();
    gs_matrix_identity();

    d_ptr->prev_target = gs_get_render_target();
    d_ptr->prev_zs = gs_get_zstencil_target();
    gs_set_render_target(d_ptr->target, d_ptr->zs);

    gs_set_viewport(0, 0, d_ptr->cx, d_ptr->cy);

    return true;
}

void gs_texture_render::gs_texrender_end()
{
    gs_set_render_target(d_ptr->prev_target.lock(), d_ptr->prev_zs.lock());

    gs_matrix_pop();
    gs_projection_pop();
    gs_viewport_pop();

    d_ptr->rendered = true;
}

void gs_texture_render::gs_texrender_reset()
{
    d_ptr->rendered = false;
}

std::shared_ptr<gs_texture> gs_texture_render::gs_texrender_get_texture()
{
    return d_ptr->target;
}
