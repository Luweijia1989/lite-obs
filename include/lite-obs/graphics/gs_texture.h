#pragma once

#include "gs_subsystem_info.h"
#include "gl_helpers.h"
#include <memory>

struct gs_zstencil_buffer {
    GLuint buffer{};
    GLuint attachment{};
    GLenum format{};

    static inline GLenum get_attachment(gs_zstencil_format format)
    {
        switch (format) {
        case gs_zstencil_format::GS_Z16:
            return GL_DEPTH_ATTACHMENT;
        case gs_zstencil_format::GS_Z24_S8:
            return GL_DEPTH_STENCIL_ATTACHMENT;
        case gs_zstencil_format::GS_Z32F:
            return GL_DEPTH_ATTACHMENT;
        case gs_zstencil_format::GS_Z32F_S8X24:
            return GL_DEPTH_STENCIL_ATTACHMENT;
        case gs_zstencil_format::GS_ZS_NONE:
            return 0;
        }

        return 0;
    }

    bool gl_init_zsbuffer(uint32_t width, uint32_t height)
    {
        glGenRenderbuffers(1, &buffer);
        if (!gl_success("glGenRenderbuffers"))
            return false;

        if (!gl_bind_renderbuffer(GL_RENDERBUFFER, buffer))
            return false;

        glRenderbufferStorage(GL_RENDERBUFFER, format, width, height);
        if (!gl_success("glRenderbufferStorage"))
            return false;

        gl_bind_renderbuffer(GL_RENDERBUFFER, 0);
        return true;
    }

    gs_zstencil_buffer(uint32_t width, uint32_t height, gs_zstencil_format f) {
        format = convert_zstencil_format(f);
        attachment = get_attachment(f);


        if (!gl_init_zsbuffer(width, height)) {
            blog(LOG_ERROR, "device_zstencil_create (GL) failed");
        }
    }

    ~gs_zstencil_buffer() {
        if (buffer) {
            glDeleteRenderbuffers(1, &buffer);
            gl_success("glDeleteRenderbuffers");
        }

    }
};

class gs_texture;
struct fbo_info {
    GLuint fbo{};
    uint32_t width{};
    uint32_t height{};
    gs_color_format format{};

    std::weak_ptr<gs_texture> cur_render_target{};
    std::weak_ptr<gs_zstencil_buffer> cur_zstencil_buffer{};

    bool attach_rendertarget(std::shared_ptr<gs_texture> tex);
    bool attach_zstencil(std::shared_ptr<gs_zstencil_buffer> zs);

    ~fbo_info();
};

struct gs_zstencil_buffer;
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
    bool gs_set_target(int side, std::shared_ptr<gs_zstencil_buffer> zs);
    bool get_tex_dimensions(uint32_t *width, uint32_t *height);

private:
    std::unique_ptr<gs_texture_private> d_ptr{};
};

std::shared_ptr<gs_texture> gs_texture_create(uint32_t width, uint32_t height, gs_color_format color_format, uint32_t flags);
std::shared_ptr<gs_texture> gs_texture_create_with_external(int texture_id, uint32_t width, uint32_t height);

