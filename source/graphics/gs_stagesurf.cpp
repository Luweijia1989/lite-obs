#include "lite-obs/graphics/gs_stagesurf.h"
#include "lite-obs/graphics/gs_subsystem_info.h"
#include "lite-obs/graphics/gl_helpers.h"
#include "lite-obs/graphics/gs_texture.h"

struct gs_stagesurface_private
{
    gs_color_format format{};
    uint32_t width{};
    uint32_t height{};
    size_t size{};

    uint32_t bytes_per_pixel{};
    GLenum gl_format{};
    GLenum gl_type{};
    GLuint pack_buffer{};

    ~gs_stagesurface_private() {
        if (pack_buffer)
            gl_delete_buffers(1, &pack_buffer);
    }
};

gs_stagesurface::gs_stagesurface()
{
    d_ptr = std::make_unique<gs_stagesurface_private>();
}

gs_stagesurface::~gs_stagesurface()
{
    blog(LOG_DEBUG, "gs_stagesurface destroyed.");
}

bool gs_stagesurface::create_pixel_pack_buffer()
{
    GLsizeiptr size;
    bool success = true;

    if (!gl_gen_buffers(1, &d_ptr->pack_buffer))
        return false;

    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, d_ptr->pack_buffer))
        return false;

    size = d_ptr->width * d_ptr->bytes_per_pixel;
    size = (size + 3) & 0xFFFFFFFC; /* align width to 4-byte boundary */
    size *= d_ptr->height;

    glBufferData(GL_PIXEL_PACK_BUFFER, size, 0, GL_DYNAMIC_READ);
    if (!gl_success("glBufferData"))
        success = false;

    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0))
        success = false;

    d_ptr->size = size;
    return success;
}

bool gs_stagesurface::gs_stagesurface_create(uint32_t width, uint32_t height, gs_color_format color_format)
{
    d_ptr->format = color_format;
    d_ptr->width = width;
    d_ptr->height = height;
    d_ptr->gl_format = convert_gs_format(color_format);
    d_ptr->gl_type = get_gl_format_type(color_format);
    d_ptr->bytes_per_pixel = gs_get_format_bpp(color_format) / 8;

    if (!create_pixel_pack_buffer()) {
        blog(LOG_ERROR, "device_stagesurface_create (GL) failed");
        return false;
    }

    return true;
}

bool gs_stagesurface::can_stage(std::shared_ptr<gs_texture> src)
{
    if (!src) {
        blog(LOG_ERROR, "Source texture is NULL");
        return false;
    }

    if (src->gs_texture_get_color_format() != d_ptr->format) {
        blog(LOG_ERROR, "Source and destination formats do not match");
        return false;
    }

    if (src->gs_texture_get_width() != d_ptr->width || src->gs_texture_get_height() != d_ptr->height) {
        blog(LOG_ERROR, "Source and destination must have the same "
                        "dimensions");
        return false;
    }

    return true;
}
#ifdef PLATFORM_MOBILE

/* Apparently for mac, PBOs won't do an asynchronous transfer unless you use
 * FBOs along with glReadPixels, which is really dumb. */
void gs_stagesurface::gs_stagesurface_stage_texture(std::shared_ptr<gs_texture> src)
{
    std::shared_ptr<fbo_info> fbo{};
    GLint last_fbo;
    bool success = false;

    if (!can_stage(src))
        goto failed;

    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, d_ptr->pack_buffer))
        goto failed;

    fbo = src->get_fbo();

    if (!gl_get_integer_v(GL_READ_FRAMEBUFFER_BINDING, &last_fbo))
        goto failed_unbind_buffer;
    if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, fbo->fbo))
        goto failed_unbind_buffer;

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, src->gs_texture_target(), src->gs_texture_obj(), 0);
    if (!gl_success("glFrameBufferTexture2D"))
        goto failed_unbind_all;

    glReadPixels(0, 0, d_ptr->width, d_ptr->height, d_ptr->gl_format, d_ptr->gl_type, 0);
    if (!gl_success("glReadPixels"))
        goto failed_unbind_all;

    success = true;

failed_unbind_all:
    gl_bind_framebuffer(GL_READ_FRAMEBUFFER, last_fbo);

failed_unbind_buffer:
    gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0);

failed:
    if (!success)
        blog(LOG_ERROR, "device_stage_texture (GL) failed");
}

#else
void gs_stagesurface::gs_stagesurface_stage_texture(std::shared_ptr<gs_texture> src)
{
    if (!can_stage(src))
        goto failed;

    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, d_ptr->pack_buffer))
        goto failed;
    if (!gl_bind_texture(GL_TEXTURE_2D, src->gs_texture_obj()))
        goto failed;

    glGetTexImage(GL_TEXTURE_2D, 0, d_ptr->gl_format, d_ptr->gl_type, 0);
    if (!gl_success("glGetTexImage"))
        goto failed;

    gl_bind_texture(GL_TEXTURE_2D, 0);
    gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
    return;

failed:
    gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
    gl_bind_texture(GL_TEXTURE_2D, 0);
    blog(LOG_ERROR, "device_stage_texture (GL) failed");
}
#endif

uint32_t gs_stagesurface::gs_stagesurface_get_width()
{
    return d_ptr->width;
}

uint32_t gs_stagesurface::gs_stagesurface_get_height()
{
    return d_ptr->height;
}

gs_color_format gs_stagesurface::gs_stagesurface_get_color_format()
{
    return d_ptr->format;
}

bool gs_stagesurface::gs_stagesurface_map(uint8_t **data, uint32_t *linesize)
{
    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, d_ptr->pack_buffer))
        goto fail;

#ifdef PLATFORM_MOBILE
    *data = (uint8_t *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, d_ptr->size, GL_MAP_READ_BIT);
#else
    *data = (uint8_t *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
#endif

    if (!gl_success("glMapBuffer"))
        goto fail;

    gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0);

    *linesize = d_ptr->bytes_per_pixel * d_ptr->width;
    return true;

fail:
    blog(LOG_ERROR, "stagesurf_map (GL) failed");
    return false;
}

void gs_stagesurface::gs_stagesurface_unmap()
{
    if (!gl_bind_buffer(GL_PIXEL_PACK_BUFFER, d_ptr->pack_buffer))
        return;

    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    gl_success("glUnmapBuffer");

    gl_bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
}

std::shared_ptr<gs_stagesurface> gs_stagesurface_create(uint32_t width, uint32_t height, gs_color_format color_format)
{
    auto surface = std::make_shared<gs_stagesurface>();
    if (!surface->gs_stagesurface_create(width, height, color_format))
        return nullptr;

    return surface;
}
