#include "lite-obs/graphics/gs_texture.h"
#include "lite-obs/graphics/gs_subsystem.h"
#include "lite-obs/graphics/gs_shader.h"

bool fbo_info::attach_rendertarget(std::shared_ptr<gs_texture> tex) {
    if (cur_render_target.lock() == tex)
        return true;

    cur_render_target = tex;

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex->gs_texture_obj(), 0);
    return gl_success("glFramebufferTexture2D");
}

fbo_info::~fbo_info() {
    glDeleteFramebuffers(1, &fbo);
    gl_success("glDeleteFramebuffers");
}

struct gs_texture_base {
    gs_color_format format{};
    GLenum gl_format{};
    GLenum gl_target{};
    GLenum gl_internal_format{};
    GLenum gl_type{};
    GLuint texture{};
    bool is_dynamic{};
    bool is_render_target{};

    std::weak_ptr<gs_sampler_state> cur_sampler;
    std::shared_ptr<fbo_info> fbo{};
};

struct gs_texture_private
{
    struct gs_texture_base base{};

    uint32_t width{};
    uint32_t height{};
    bool gen_mipmaps{};
    GLuint unpack_buffer{};
    GLsizeiptr size{};

    bool external_texture{};
};

gs_texture::gs_texture()
{
    d_ptr = std::make_unique<gs_texture_private>();
}

gs_texture::~gs_texture()
{
    if (!d_ptr->external_texture) {
        if (d_ptr->base.is_dynamic && d_ptr->unpack_buffer)
            gl_delete_buffers(1, &d_ptr->unpack_buffer);

        if (d_ptr->base.texture)
            gl_delete_textures(1, &d_ptr->base.texture);
    }

    blog(LOG_DEBUG, "gs_texture destroyed.");
}

bool gs_texture::create(uint32_t width, uint32_t height, gs_color_format color_format, uint32_t flags)
{
    d_ptr->base.format = color_format;
    d_ptr->base.gl_format = convert_gs_format(color_format);
    d_ptr->base.gl_internal_format = convert_gs_internal_format(color_format);
    d_ptr->base.gl_type = get_gl_format_type(color_format);
    d_ptr->base.gl_target = GL_TEXTURE_2D;
    d_ptr->base.is_dynamic = (flags & GS_DYNAMIC) != 0;
    d_ptr->base.is_render_target = (flags & GS_RENDER_TARGET) != 0;
    d_ptr->width = width;
    d_ptr->height = height;

    if (!gl_gen_textures(1, &d_ptr->base.texture))
        return false;

    if (d_ptr->base.is_dynamic && !create_pixel_unpack_buffer())
        return false;
    if (!allocate_texture_mem())
        return false;

    return true;
}

void gs_texture::create(int texture_id, uint32_t width, uint32_t height)
{
    d_ptr->base.format = gs_color_format::GS_RGBA;
    d_ptr->base.gl_format = GL_RGBA;
    d_ptr->base.gl_internal_format = GL_RGBA;
    d_ptr->base.gl_type = GL_UNSIGNED_BYTE;
    d_ptr->base.gl_target = GL_TEXTURE_2D;
    d_ptr->base.is_dynamic = false;
    d_ptr->base.is_render_target = false;
    d_ptr->width = width;
    d_ptr->height = height;
    d_ptr->base.texture = texture_id;
    d_ptr->external_texture = true;
}

uint32_t gs_texture::gs_texture_get_width()
{
    return d_ptr->width;
}

uint32_t gs_texture::gs_texture_get_height()
{
    return d_ptr->height;
}

gs_color_format gs_texture::gs_texture_get_color_format()
{
    return d_ptr->base.format;
}

void gs_texture::gs_texture_set_image(const uint8_t *data, uint32_t linesize, bool flip)
{
    uint8_t *ptr;
    uint32_t linesize_out;
    uint32_t row_copy;
    int32_t height;
    int32_t y;

    height = (int32_t)d_ptr->height;

    if (!gs_texture_map(&ptr, &linesize_out))
        return;

    row_copy = (linesize < linesize_out) ? linesize : linesize_out;

    if (flip) {
        for (y = height - 1; y >= 0; y--)
            memcpy(ptr + (uint32_t)y * linesize_out,
                   data + (uint32_t)(height - y - 1) * linesize,
                   row_copy);

    } else if (linesize == linesize_out) {
        memcpy(ptr, data, row_copy * height);

    } else {
        for (y = 0; y < height; y++)
            memcpy(ptr + (uint32_t)y * linesize_out,
                   data + (uint32_t)y * linesize, row_copy);
    }

    gs_texture_unmap();
}

bool gs_texture::gs_texture_map(uint8_t **ptr, uint32_t *linesize)
{
    if (!d_ptr->base.is_dynamic) {
        blog(LOG_ERROR, "Texture is not dynamic");
        goto fail;
    }

    if (!gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, d_ptr->unpack_buffer))
        goto fail;

#ifdef PLATFORM_MOBILE
    *ptr = (uint8_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, d_ptr->size, GL_MAP_WRITE_BIT);
#else
    *ptr = (uint8_t *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
#endif
    if (!gl_success("glMapBuffer"))
        goto fail;

    gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);

    *linesize = d_ptr->width * gs_get_format_bpp(d_ptr->base.format) / 8;
    *linesize = (*linesize + 3) & 0xFFFFFFFC;
    return true;

fail:
    blog(LOG_ERROR, "gs_texture_map (GL) failed");
    return false;
}

void gs_texture::gs_texture_unmap()
{
    if (!gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, d_ptr->unpack_buffer))
        goto failed;

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    if (!gl_success("glUnmapBuffer"))
        goto failed;

    if (!gl_bind_texture(GL_TEXTURE_2D, d_ptr->base.texture))
        goto failed;

    glTexImage2D(GL_TEXTURE_2D, 0, d_ptr->base.gl_internal_format, d_ptr->width,
                 d_ptr->height, 0, d_ptr->base.gl_format, d_ptr->base.gl_type, 0);
    if (!gl_success("glTexImage2D"))
        goto failed;

    gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl_bind_texture(GL_TEXTURE_2D, 0);
    return;

failed:
    gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl_bind_texture(GL_TEXTURE_2D, 0);
    blog(LOG_ERROR, "gs_texture_unmap (GL) failed");
}

bool gs_texture::gs_texture_is_rect()
{
    return false;
}

bool gs_texture::gs_texture_is_render_target()
{
    return d_ptr->base.is_render_target;
}

std::shared_ptr<fbo_info> gs_texture::get_fbo()
{
    uint32_t width, height;
    if (!get_tex_dimensions( &width, &height))
        return nullptr;

    if (d_ptr->base.fbo && d_ptr->base.fbo->width == width &&
        d_ptr->base.fbo->height == height && d_ptr->base.fbo->format == d_ptr->base.format)
        return d_ptr->base.fbo;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    if (!gl_success("glGenFramebuffers"))
        return nullptr;

    d_ptr->base.fbo = std::make_shared<fbo_info>();

    d_ptr->base.fbo->fbo = fbo;
    d_ptr->base.fbo->width = width;
    d_ptr->base.fbo->height = height;
    d_ptr->base.fbo->format = d_ptr->base.format;
    d_ptr->base.fbo->cur_render_target.reset();

    return d_ptr->base.fbo;
}

GLuint gs_texture::gs_texture_obj()
{
    return d_ptr->base.texture;
}

GLenum gs_texture::gs_texture_target()
{
    return d_ptr->base.gl_target;
}

bool gs_texture::gs_texture_load_texture_sampler(std::shared_ptr<gs_sampler_state> ss)
{
    if (d_ptr->external_texture)
        return true;

    auto cur_sampler = d_ptr->base.cur_sampler.lock();
    if (cur_sampler == ss)
        return true;

    d_ptr->base.cur_sampler = ss;
    if (!ss)
        return true;

    bool success = true;
    if (!gl_tex_param_i(d_ptr->base.gl_target, GL_TEXTURE_MIN_FILTER, ss->min_filter))
        success = false;
    if (!gl_tex_param_i(d_ptr->base.gl_target, GL_TEXTURE_MAG_FILTER, ss->mag_filter))
        success = false;
    if (!gl_tex_param_i(d_ptr->base.gl_target, GL_TEXTURE_WRAP_S, ss->address_u))
        success = false;
    if (!gl_tex_param_i(d_ptr->base.gl_target, GL_TEXTURE_WRAP_T, ss->address_v))
        success = false;
    if (!gl_tex_param_i(d_ptr->base.gl_target, GL_TEXTURE_WRAP_R, ss->address_w))
        success = false;

    return success;
}

bool gs_texture::gs_texture_copy(const std::shared_ptr<gs_texture> &src)
{
    auto fbo = src->get_fbo();
    GLint last_fbo;
    bool success = false;

    if (!fbo)
        return false;

    if (!gl_get_integer_v(GL_READ_FRAMEBUFFER_BINDING, &last_fbo))
        return false;
    if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, fbo->fbo))
        return false;
    if (!gl_bind_texture(d_ptr->base.gl_target, d_ptr->base.texture))
        goto fail;

    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0,
                           src->d_ptr->base.gl_target, src->d_ptr->base.texture, 0);
    if (!gl_success("glFrameBufferTexture2D"))
        goto fail;

    glReadBuffer(GL_COLOR_ATTACHMENT0 + 0);
    if (!gl_success("glReadBuffer"))
        goto fail;

    glCopyTexSubImage2D(d_ptr->base.gl_target, 0, 0, 0, 0, 0, src->d_ptr->width, src->d_ptr->height);
    if (!gl_success("glCopyTexSubImage2D"))
        goto fail;

    success = true;

fail:
    if (!gl_bind_texture(d_ptr->base.gl_target, 0))
        success = false;
    if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, last_fbo))
        success = false;

    return success;
}

bool gs_texture::allocate_texture_mem()
{
    if (!gl_bind_texture(GL_TEXTURE_2D, d_ptr->base.texture))
        return false;

#ifdef PLATFORM_MOBILE
    //since opengles glTexImage2D do not support inter_format GL_RGB format GL_BGRA, convert GL_BGRA to GL_RGB, it's ok because there is no data to upload here.
    auto format = d_ptr->base.gl_format == GL_BGRA ? GL_RGB : d_ptr->base.gl_format;
    glTexImage2D(GL_TEXTURE_2D, 0, d_ptr->base.gl_internal_format, d_ptr->width, d_ptr->height, 0, format, d_ptr->base.gl_type, NULL);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, d_ptr->base.gl_internal_format, d_ptr->width, d_ptr->height, 0, d_ptr->base.gl_format, d_ptr->base.gl_type, NULL);
#endif
    if (!gl_success("glTexImage2D"))
        return false;

    if (!gl_tex_param_i(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0) || !gl_bind_texture(GL_TEXTURE_2D, 0))
        return false;

    return true;
}

bool gs_texture::get_tex_dimensions(uint32_t *width, uint32_t *height)
{
    *width = d_ptr->width;
    *height = d_ptr->height;
    return true;
}

bool gs_texture::create_pixel_unpack_buffer()
{
    GLsizeiptr size;
    bool success = true;

    if (!gl_gen_buffers(1, &d_ptr->unpack_buffer))
        return false;

    if (!gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, d_ptr->unpack_buffer))
        return false;

    size = d_ptr->width * gs_get_format_bpp(d_ptr->base.format);
    size /= 8;
    size = (size + 3) & 0xFFFFFFFC;
    size *= d_ptr->height;

    glBufferData(GL_PIXEL_UNPACK_BUFFER, size, 0, GL_DYNAMIC_DRAW);
    if (!gl_success("glBufferData"))
        success = false;

    if (!gl_bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0))
        success = false;

    d_ptr->size = size;
    return success;
}

std::shared_ptr<gs_texture> gs_texture_create(uint32_t width, uint32_t height, gs_color_format color_format, uint32_t flags)
{
    auto tex = std::make_shared<gs_texture>();
    if (!tex->create(width, height, color_format, flags)) {
        blog(LOG_DEBUG, "Cannot create texture, width: %d, height:%d, format: %d", width, height, (int)color_format);
        return nullptr;
    }

    blog(LOG_DEBUG, "gs_texture_create: success.");

    return tex;
}

std::shared_ptr<gs_texture> gs_texture_create_with_external(int texture_id, uint32_t width, uint32_t height)
{
    auto tex = std::make_shared<gs_texture>();
    tex->create(texture_id, width, height);
    return tex;
}
