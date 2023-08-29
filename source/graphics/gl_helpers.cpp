#include "lite-obs/graphics/gl_helpers.h"
#include <memory>

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
