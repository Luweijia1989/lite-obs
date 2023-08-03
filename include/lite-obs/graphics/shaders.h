#pragma once

#include "gs_shader_info.h"

static enum gs_shader_param_type get_shader_param_type(const char *type)
{
    if (strcmp(type, "float") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_FLOAT;
    else if (strcmp(type, "float2") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_VEC2;
    else if (strcmp(type, "float3") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_VEC3;
    else if (strcmp(type, "float4") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_VEC4;
    else if (strcmp(type, "int2") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_INT2;
    else if (strcmp(type, "int3") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_INT3;
    else if (strcmp(type, "int4") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_INT4;
    else if (strncmp(type, "texture", 7) == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_TEXTURE;
    else if (strcmp(type, "float4x4") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_MATRIX4X4;
    else if (strcmp(type, "bool") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_BOOL;
    else if (strcmp(type, "int") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_INT;
    else if (strcmp(type, "string") == 0)
        return gs_shader_param_type::GS_SHADER_PARAM_STRING;

    return gs_shader_param_type::GS_SHADER_PARAM_UNKNOWN;
}

int astrcmpi(const char *str1, const char *str2);

static gs_sample_filter get_sample_filter(const char *filter)
{
    if (astrcmpi(filter, "Anisotropy") == 0)
        return gs_sample_filter::GS_FILTER_ANISOTROPIC;

    else if (astrcmpi(filter, "Point") == 0 ||
         strcmp(filter, "MIN_MAG_MIP_POINT") == 0)
        return gs_sample_filter::GS_FILTER_POINT;

    else if (astrcmpi(filter, "Linear") == 0 ||
         strcmp(filter, "MIN_MAG_MIP_LINEAR") == 0)
        return gs_sample_filter::GS_FILTER_LINEAR;

    else if (strcmp(filter, "MIN_MAG_POINT_MIP_LINEAR") == 0)
        return gs_sample_filter::GS_FILTER_MIN_MAG_POINT_MIP_LINEAR;

    else if (strcmp(filter, "MIN_POINT_MAG_LINEAR_MIP_POINT") == 0)
        return gs_sample_filter::GS_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;

    else if (strcmp(filter, "MIN_POINT_MAG_MIP_LINEAR") == 0)
        return gs_sample_filter::GS_FILTER_MIN_POINT_MAG_MIP_LINEAR;

    else if (strcmp(filter, "MIN_LINEAR_MAG_MIP_POINT") == 0)
        return gs_sample_filter::GS_FILTER_MIN_LINEAR_MAG_MIP_POINT;

    else if (strcmp(filter, "MIN_LINEAR_MAG_POINT_MIP_LINEAR") == 0)
        return gs_sample_filter::GS_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;

    else if (strcmp(filter, "MIN_MAG_LINEAR_MIP_POINT") == 0)
        return gs_sample_filter::GS_FILTER_MIN_MAG_LINEAR_MIP_POINT;

    return gs_sample_filter::GS_FILTER_LINEAR;
}

static gs_address_mode get_address_mode(const char *mode)
{
    if (astrcmpi(mode, "Wrap") == 0 || astrcmpi(mode, "Repeat") == 0)
        return gs_address_mode::GS_ADDRESS_WRAP;
    else if (astrcmpi(mode, "Clamp") == 0 || astrcmpi(mode, "None") == 0)
        return gs_address_mode::GS_ADDRESS_CLAMP;
    else if (astrcmpi(mode, "Mirror") == 0)
        return gs_address_mode::GS_ADDRESS_MIRROR;

    return gs_address_mode::GS_ADDRESS_CLAMP;
}

static inline void convert_filter(gs_sample_filter filter,
                  GLint *min_filter, GLint *mag_filter)
{
    switch (filter) {
    case gs_sample_filter::GS_FILTER_POINT:
        *min_filter = GL_NEAREST_MIPMAP_NEAREST;
        *mag_filter = GL_NEAREST;
        return;
    case gs_sample_filter::GS_FILTER_LINEAR:
        *min_filter = GL_LINEAR_MIPMAP_LINEAR;
        *mag_filter = GL_LINEAR;
        return;
    case gs_sample_filter::GS_FILTER_MIN_MAG_POINT_MIP_LINEAR:
        *min_filter = GL_NEAREST_MIPMAP_LINEAR;
        *mag_filter = GL_NEAREST;
        return;
    case gs_sample_filter::GS_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
        *min_filter = GL_NEAREST_MIPMAP_NEAREST;
        *mag_filter = GL_LINEAR;
        return;
    case gs_sample_filter::GS_FILTER_MIN_POINT_MAG_MIP_LINEAR:
        *min_filter = GL_NEAREST_MIPMAP_LINEAR;
        *mag_filter = GL_LINEAR;
        return;
    case gs_sample_filter::GS_FILTER_MIN_LINEAR_MAG_MIP_POINT:
        *min_filter = GL_LINEAR_MIPMAP_NEAREST;
        *mag_filter = GL_NEAREST;
        return;
    case gs_sample_filter::GS_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
        *min_filter = GL_LINEAR_MIPMAP_LINEAR;
        *mag_filter = GL_NEAREST;
        return;
    case gs_sample_filter::GS_FILTER_MIN_MAG_LINEAR_MIP_POINT:
        *min_filter = GL_LINEAR_MIPMAP_NEAREST;
        *mag_filter = GL_LINEAR;
        return;
    case gs_sample_filter::GS_FILTER_ANISOTROPIC:
        *min_filter = GL_LINEAR_MIPMAP_LINEAR;
        *mag_filter = GL_LINEAR;
        return;
    }

    *min_filter = GL_NEAREST_MIPMAP_NEAREST;
    *mag_filter = GL_NEAREST;
}

static inline GLint convert_address_mode(gs_address_mode mode)
{
    switch (mode) {
    case gs_address_mode::GS_ADDRESS_WRAP:
        return GL_REPEAT;
    case gs_address_mode::GS_ADDRESS_CLAMP:
        return GL_CLAMP_TO_EDGE;
    case gs_address_mode::GS_ADDRESS_MIRROR:
        return GL_MIRRORED_REPEAT;
    }

    return GL_REPEAT;
}

enum class shader_var_type {
    SHADER_VAR_NONE,
    SHADER_VAR_IN = SHADER_VAR_NONE,
    SHADER_VAR_INOUT,
    SHADER_VAR_OUT,
    SHADER_VAR_UNIFORM,
    SHADER_VAR_CONST
};

struct gl_parser_shader_var
{
    std::string type{};
    std::string name{};
    std::string mapping{};
    shader_var_type var_type{};
    size_t gl_sampler_id{}; /* optional: used/parsed by GL */

    std::vector<uint8_t> default_val{};
};

struct gl_parser_attrib {
    std::string name{};
    std::string mapping{};
    bool input{};
};

struct gl_parser_shader_sampler
{
    std::string name{};
    std::vector<std::string> states{};
    std::vector<std::string> values;
};

struct gs_shader_info
{
    gs_shader_type type{};
    std::string shader{};
    std::vector<gl_parser_shader_var> parser_shader_vars{};
    std::vector<gl_parser_attrib> parser_attribs{};
    std::vector<gl_parser_shader_sampler> parser_shader_samplers;
};

struct gs_shader_item
{
    std::string name {};
    std::vector<gs_shader_info> shaders;
};

extern std::string conversion_shaders_total;
