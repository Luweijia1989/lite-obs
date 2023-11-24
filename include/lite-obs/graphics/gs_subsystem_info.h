#pragma once

#include "lite-obs/lite_obs_platform_config.h"

#if TARGET_PLATFORM == PLATFORM_IOS

#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

#elif TARGET_PLATFORM == PLATFORM_ANDROID

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#define GL_BGRA GL_BGRA_EXT

#elif TARGET_PLATFORM == PLATFORM_MAC

#include <OpenGL/gl3.h>
#include <OpenGL/glext.h>

#elif TARGET_PLATFORM == PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <glad/glad_wgl.h>

#endif

#include <stdint.h>

#define GS_SUCCESS 0
#define GS_ERROR_FAIL -1

#define GS_DYNAMIC (1 << 0)
#define GS_RENDER_TARGET (1 << 1)


#define GS_FLIP_U (1 << 0)
#define GS_FLIP_V (1 << 1)

#define GS_MAX_TEXTURES 8

enum class gs_blend_type {
    GS_BLEND_ZERO,
    GS_BLEND_ONE,
    GS_BLEND_SRCCOLOR,
    GS_BLEND_INVSRCCOLOR,
    GS_BLEND_SRCALPHA,
    GS_BLEND_INVSRCALPHA,
    GS_BLEND_DSTCOLOR,
    GS_BLEND_INVDSTCOLOR,
    GS_BLEND_DSTALPHA,
    GS_BLEND_INVDSTALPHA,
    GS_BLEND_SRCALPHASAT,
};

static inline GLenum convert_gs_blend_type(gs_blend_type type)
{
    switch (type) {
    case gs_blend_type::GS_BLEND_ZERO:
        return GL_ZERO;
    case gs_blend_type::GS_BLEND_ONE:
        return GL_ONE;
    case gs_blend_type::GS_BLEND_SRCCOLOR:
        return GL_SRC_COLOR;
    case gs_blend_type::GS_BLEND_INVSRCCOLOR:
        return GL_ONE_MINUS_SRC_COLOR;
    case gs_blend_type::GS_BLEND_SRCALPHA:
        return GL_SRC_ALPHA;
    case gs_blend_type::GS_BLEND_INVSRCALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    case gs_blend_type::GS_BLEND_DSTCOLOR:
        return GL_DST_COLOR;
    case gs_blend_type::GS_BLEND_INVDSTCOLOR:
        return GL_ONE_MINUS_DST_COLOR;
    case gs_blend_type::GS_BLEND_DSTALPHA:
        return GL_DST_ALPHA;
    case gs_blend_type::GS_BLEND_INVDSTALPHA:
        return GL_ONE_MINUS_DST_ALPHA;
    case gs_blend_type::GS_BLEND_SRCALPHASAT:
        return GL_SRC_ALPHA_SATURATE;
    }

    return GL_ONE;
}

enum class gs_color_format {
    GS_UNKNOWN,
    GS_R8,
    GS_RGBA,

    GS_BGRA, //use for source video frame
    GS_BGRX,

    GS_R8G8,
};

static inline uint32_t gs_get_format_bpp(gs_color_format format)
{
    switch (format) {
    case gs_color_format::GS_R8:
        return 8;
    case gs_color_format::GS_RGBA:
        return 32;
    case gs_color_format::GS_BGRX:
        return 32;
    case gs_color_format::GS_BGRA:
        return 32;
    case gs_color_format::GS_R8G8:
        return 16;
    case gs_color_format::GS_UNKNOWN:
        return 0;
    }

    return 0;
}

static inline GLenum convert_gs_format(gs_color_format format)
{
    switch (format) {
    case gs_color_format::GS_R8:
        return GL_RED;
    case gs_color_format::GS_RGBA:
        return GL_RGBA;
    case gs_color_format::GS_BGRX:
        return GL_BGRA;
    case gs_color_format::GS_BGRA:
        return GL_BGRA;
    case gs_color_format::GS_R8G8:
        return GL_RG;
    case gs_color_format::GS_UNKNOWN:
        return 0;
    }

    return 0;
}

static inline GLenum convert_gs_internal_format(gs_color_format format)
{
    switch (format) {
    case gs_color_format::GS_R8:
        return GL_R8;
    case gs_color_format::GS_RGBA:
        return GL_RGBA;
    case gs_color_format::GS_BGRX:
        return GL_RGB;
    case gs_color_format::GS_BGRA:
        return GL_RGBA;
    case gs_color_format::GS_R8G8:
        return GL_RG8;
    case gs_color_format::GS_UNKNOWN:
        return 0;
    }

    return 0;
}

static inline GLenum get_gl_format_type(gs_color_format format)
{
    switch (format) {
    case gs_color_format::GS_R8:
        return GL_UNSIGNED_BYTE;
    case gs_color_format::GS_RGBA:
        return GL_UNSIGNED_BYTE;
    case gs_color_format::GS_BGRX:
        return GL_UNSIGNED_BYTE;
    case gs_color_format::GS_BGRA:
        return GL_UNSIGNED_BYTE;
    case gs_color_format::GS_R8G8:
        return GL_UNSIGNED_BYTE;
    case gs_color_format::GS_UNKNOWN:
        return 0;
    }

    return GL_UNSIGNED_BYTE;
}

#define GS_CLEAR_COLOR (1 << 0)
#define GS_CLEAR_DEPTH (1 << 1)
#define GS_CLEAR_STENCIL (1 << 2)

enum class gs_draw_mode {
    GS_POINTS,
    GS_LINES,
    GS_LINESTRIP,
    GS_TRIS,
    GS_TRISTRIP,
};

enum class attrib_type {
    ATTRIB_POSITION,
    ATTRIB_NORMAL,
    ATTRIB_TANGENT,
    ATTRIB_COLOR,
    ATTRIB_TEXCOORD,
    ATTRIB_TARGET
};

static inline GLenum convert_gs_topology(gs_draw_mode mode)
{
    switch (mode) {
    case gs_draw_mode::GS_POINTS:
        return GL_POINTS;
    case gs_draw_mode::GS_LINES:
        return GL_LINES;
    case gs_draw_mode::GS_LINESTRIP:
        return GL_LINE_STRIP;
    case gs_draw_mode::GS_TRIS:
        return GL_TRIANGLES;
    case gs_draw_mode::GS_TRISTRIP:
        return GL_TRIANGLE_STRIP;
    }

    return GL_POINTS;
}

struct gs_rect {
    int x;
    int y;
    int cx;
    int cy;

    bool is_null() {
        return x == 0 && y == 0 && cx == 0 && cy == 0;
    }
};
