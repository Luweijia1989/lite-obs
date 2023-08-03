#include "lite-obs/graphics/gl-helpers.h"
#include <memory>

bool gl_init_face(GLenum target, GLenum type,
                  GLenum format, GLint internal_format,
                  uint32_t width, uint32_t height, uint32_t size,
                  const uint8_t **p_data)
{
    glTexImage2D(target, 0, internal_format, width, height, 0, format, type, p_data ? *p_data : NULL);
    if (!gl_success("glTexImage2D"))
        return false;

    return true;
}

//static bool gl_copy_fbo(struct gs_texture *dst, uint32_t dst_x, uint32_t dst_y,
//			struct gs_texture *src, uint32_t src_x, uint32_t src_y,
//			uint32_t width, uint32_t height)
//{
//	struct fbo_info *fbo = get_fbo(src, width, height);
//	GLint last_fbo;
//	bool success = false;

//	if (!fbo)
//		return false;

//	if (!gl_get_integer_v(GL_READ_FRAMEBUFFER_BINDING, &last_fbo))
//		return false;
//	if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, fbo->fbo))
//		return false;
//	if (!gl_bind_texture(dst->gl_target, dst->texture))
//		goto fail;

//	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0,
//			       src->gl_target, src->texture, 0);
//	if (!gl_success("glFrameBufferTexture2D"))
//		goto fail;

//	glReadBuffer(GL_COLOR_ATTACHMENT0 + 0);
//	if (!gl_success("glReadBuffer"))
//		goto fail;

//	glCopyTexSubImage2D(dst->gl_target, 0, dst_x, dst_y, src_x, src_y,
//			    width, height);
//	if (!gl_success("glCopyTexSubImage2D"))
//		goto fail;

//	success = true;

//fail:
//	if (!gl_bind_texture(dst->gl_target, 0))
//		success = false;
//	if (!gl_bind_framebuffer(GL_READ_FRAMEBUFFER, last_fbo))
//		success = false;

//	return success;
//}

//bool gl_copy_texture(struct gs_device *device, struct gs_texture *dst,
//		     uint32_t dst_x, uint32_t dst_y, struct gs_texture *src,
//		     uint32_t src_x, uint32_t src_y, uint32_t width,
//		     uint32_t height)
//{
//	bool success = false;

//	if (device->copy_type == COPY_TYPE_ARB) {
//		glCopyImageSubData(src->texture, src->gl_target, 0, src_x,
//				   src_y, 0, dst->texture, dst->gl_target, 0,
//				   dst_x, dst_y, 0, width, height, 1);
//		success = gl_success("glCopyImageSubData");

//	} else if (device->copy_type == COPY_TYPE_NV) {
//		glCopyImageSubDataNV(src->texture, src->gl_target, 0, src_x,
//				     src_y, 0, dst->texture, dst->gl_target, 0,
//				     dst_x, dst_y, 0, width, height, 1);
//		success = gl_success("glCopyImageSubDataNV");

//	} else if (device->copy_type == COPY_TYPE_FBO_BLIT) {
//		success = gl_copy_fbo(dst, dst_x, dst_y, src, src_x, src_y,
//				      width, height);
//		if (!success)
//			blog(LOG_ERROR, "gl_copy_texture failed");
//	}

//	return success;
//}

bool gl_create_buffer(GLenum target, GLuint *buffer, GLsizeiptr size,
                      const GLvoid *data, GLenum usage)
{
    bool success;
    if (!gl_gen_buffers(1, buffer))
        return false;
    if (!gl_bind_buffer(target, *buffer))
        return false;

    glBufferData(target, size, data, usage);
    success = gl_success("glBufferData");

    gl_bind_buffer(target, 0);
    return success;
}

bool update_buffer(GLenum target, GLuint buffer, const void *data, size_t size)
{
    void *ptr;
    bool success = true;

    if (!gl_bind_buffer(target, buffer))
        return false;

    /* glMapBufferRange with these flags will actually give far better
     * performance than a plain glMapBuffer call */
    ptr = glMapBufferRange(target, 0, size,
                           GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    success = gl_success("glMapBufferRange");
    if (success && ptr) {
        memcpy(ptr, data, size);
        glUnmapBuffer(target);
    }

    gl_bind_buffer(target, 0);
    return success;
}
