#pragma once

#include "gs_subsystem_info.h"
#include "lite-obs/util/log.h"

static const char *gl_error_to_str(GLenum errorcode)
{
    static const struct {
        GLenum error;
        const char *str;
    } err_to_str[] = {
    {
        GL_INVALID_ENUM,
                "GL_INVALID_ENUM",
    },
    {
        GL_INVALID_VALUE,
                "GL_INVALID_VALUE",
    },
    {
        GL_INVALID_OPERATION,
                "GL_INVALID_OPERATION",
    },
    {
        GL_INVALID_FRAMEBUFFER_OPERATION,
                "GL_INVALID_FRAMEBUFFER_OPERATION",
    },
    {
        GL_OUT_OF_MEMORY,
                "GL_OUT_OF_MEMORY",
    },
};
    for (size_t i = 0; i < sizeof(err_to_str) / sizeof(*err_to_str); i++) {
        if (err_to_str[i].error == errorcode)
            return err_to_str[i].str;
    }
    return "Unknown";
}

/*
 * Okay, so GL error handling is..  unclean to work with.  I don't want
 * to have to keep typing out the same stuff over and over again do I'll just
 * make a bunch of helper functions to make it a bit easier to handle errors
 */

static inline bool gl_success(const char *funcname)
{
    GLenum errorcode = glGetError();
    if (errorcode != GL_NO_ERROR) {
        int attempts = 8;
        do {
            blog(LOG_ERROR,
                 "%s failed, glGetError returned %s(0x%X)",
                 funcname, gl_error_to_str(errorcode), errorcode);
            errorcode = glGetError();

            --attempts;
            if (attempts == 0) {
                blog(LOG_ERROR,
                     "Too many GL errors, moving on");
                break;
            }
        } while (errorcode != GL_NO_ERROR);
        return false;
    }

    return true;
}

static inline bool gl_gen_textures(GLsizei num_texture, GLuint *textures)
{
    glGenTextures(num_texture, textures);
    return gl_success("glGenTextures");
}

static inline bool gl_bind_texture(GLenum target, GLuint texture)
{
    glBindTexture(target, texture);
    return gl_success("glBindTexture");
}

static inline void gl_delete_textures(GLsizei num_buffers, GLuint *buffers)
{
    glDeleteTextures(num_buffers, buffers);
    gl_success("glDeleteTextures");
}

static inline bool gl_gen_buffers(GLsizei num_buffers, GLuint *buffers)
{
    glGenBuffers(num_buffers, buffers);
    return gl_success("glGenBuffers");
}

static inline bool gl_bind_buffer(GLenum target, GLuint buffer)
{
    glBindBuffer(target, buffer);
    return gl_success("glBindBuffer");
}

static inline void gl_delete_buffers(GLsizei num_buffers, GLuint *buffers)
{
    glDeleteBuffers(num_buffers, buffers);
    gl_success("glDeleteBuffers");
}

static inline bool gl_gen_vertex_arrays(GLsizei num_arrays, GLuint *arrays)
{
    glGenVertexArrays(num_arrays, arrays);
    return gl_success("glGenVertexArrays");
}

static inline bool gl_bind_vertex_array(GLuint array)
{
    glBindVertexArray(array);
    return gl_success("glBindVertexArray");
}

static inline void gl_delete_vertex_arrays(GLsizei num_arrays, GLuint *arrays)
{
    glDeleteVertexArrays(num_arrays, arrays);
    gl_success("glDeleteVertexArrays");
}

static inline bool gl_bind_renderbuffer(GLenum target, GLuint buffer)
{
    glBindRenderbuffer(target, buffer);
    return gl_success("glBindRendebuffer");
}

static inline bool gl_bind_framebuffer(GLenum target, GLuint buffer)
{
    glBindFramebuffer(target, buffer);
    return gl_success("glBindFramebuffer");
}

static inline bool gl_tex_param_f(GLenum target, GLenum param, GLfloat val)
{
    glTexParameterf(target, param, val);
    return gl_success("glTexParameterf");
}

static inline bool gl_tex_param_i(GLenum target, GLenum param, GLint val)
{
    glTexParameteri(target, param, val);
    return gl_success("glTexParameteri");
}

static inline bool gl_active_texture(GLenum texture_id)
{
    glActiveTexture(texture_id);
    return gl_success("glActiveTexture");
}

static inline bool gl_enable(GLenum capability)
{
    glEnable(capability);
    return gl_success("glEnable");
}

static inline bool gl_disable(GLenum capability)
{
    glDisable(capability);
    return gl_success("glDisable");
}

static inline bool gl_cull_face(GLenum faces)
{
    glCullFace(faces);
    return gl_success("glCullFace");
}

static inline bool gl_get_integer_v(GLenum pname, GLint *params)
{
    glGetIntegerv(pname, params);
    return gl_success("glGetIntegerv");
}

bool gl_create_buffer(GLenum target, GLuint *buffer, GLsizeiptr size,
                      const GLvoid *data, GLenum usage);

bool update_buffer(GLenum target, GLuint buffer, const void *data, size_t size);
