#pragma once

#include <vector>
#include <string>
#include <memory>
#include "gs_subsystem_info.h"

enum class gs_shader_type {
    GS_SHADER_VERTEX,
    GS_SHADER_PIXEL,
};

enum class gs_shader_param_type {
    GS_SHADER_PARAM_UNKNOWN,
    GS_SHADER_PARAM_BOOL,
    GS_SHADER_PARAM_FLOAT,
    GS_SHADER_PARAM_INT,
    GS_SHADER_PARAM_STRING,
    GS_SHADER_PARAM_VEC2,
    GS_SHADER_PARAM_VEC3,
    GS_SHADER_PARAM_VEC4,
    GS_SHADER_PARAM_INT2,
    GS_SHADER_PARAM_INT3,
    GS_SHADER_PARAM_INT4,
    GS_SHADER_PARAM_MATRIX4X4,
    GS_SHADER_PARAM_TEXTURE,
};

enum class gs_sample_filter {
    GS_FILTER_POINT,
    GS_FILTER_LINEAR,
    GS_FILTER_ANISOTROPIC,
    GS_FILTER_MIN_MAG_POINT_MIP_LINEAR,
    GS_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
    GS_FILTER_MIN_POINT_MAG_MIP_LINEAR,
    GS_FILTER_MIN_LINEAR_MAG_MIP_POINT,
    GS_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
    GS_FILTER_MIN_MAG_LINEAR_MIP_POINT,
};

enum class gs_address_mode {
    GS_ADDRESS_CLAMP,
    GS_ADDRESS_WRAP,
    GS_ADDRESS_MIRROR,
};

struct shader_attrib {
    std::string name{};
    size_t index{};
    attrib_type type{};
};

class gs_texture;
struct gs_shader_param {
    gs_shader_param_type type{};

    std::string name{};

    GLint texture_id{};
    size_t sampler_id{};

    std::weak_ptr<gs_texture> texture;

    std::vector<uint8_t> cur_value{};
    std::vector<uint8_t> def_value{};
    bool changed{};

    ~gs_shader_param() {
    }
};
