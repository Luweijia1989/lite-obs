#pragma once

#include "gs_subsystem_info.h"
#include "gl_helpers.h"
#include <memory>

class gs_texture;
struct fbo_info {
    GLuint fbo{};
    uint32_t width{};
    uint32_t height{};
    gs_color_format format{};

    std::weak_ptr<gs_texture> cur_render_target{};

    bool attach_rendertarget(std::shared_ptr<gs_texture> tex);

    ~fbo_info();
};

struct gs_sampler_state;
struct gs_texture_private;
class gs_texture
{
public:
    gs_texture();
    ~gs_texture();

    bool create(uint32_t width, uint32_t height, gs_color_format color_format, uint32_t flags);
    void create(int texture_id, uint32_t width, uint32_t height);

    uint32_t gs_texture_get_width();
    uint32_t gs_texture_get_height();

    gs_color_format gs_texture_get_color_format();

    void gs_texture_set_image(const uint8_t *data, uint32_t linesize, bool flip);

    bool gs_texture_map(uint8_t **ptr, uint32_t *linesize);
    void gs_texture_unmap();

    bool gs_texture_is_rect();
    bool gs_texture_is_render_target();

    std::shared_ptr<fbo_info> get_fbo();
    GLuint gs_texture_obj();
    GLenum gs_texture_target();

    bool gs_texture_load_texture_sampler(std::shared_ptr<gs_sampler_state> ss);

    bool gs_texture_copy(const std::shared_ptr<gs_texture> &src);

private:
    bool create_pixel_unpack_buffer();
    bool allocate_texture_mem();
    bool get_tex_dimensions(uint32_t *width, uint32_t *height);

private:
    std::unique_ptr<gs_texture_private> d_ptr{};
};

std::shared_ptr<gs_texture> gs_texture_create(uint32_t width, uint32_t height, gs_color_format color_format, uint32_t flags);
std::shared_ptr<gs_texture> gs_texture_create_with_external(int texture_id, uint32_t width, uint32_t height);

