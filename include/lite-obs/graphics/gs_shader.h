#pragma once

#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>
#include "shaders.h"
#include "gs_subsystem_info.h"

struct gs_sampler_state {
    GLint min_filter{};
    GLint mag_filter{};
    GLint address_u{};
    GLint address_v{};
    GLint address_w{};
};

struct gs_shader_private;
struct gs_shader_param;
class gs_shader
{
public:
    gs_shader();
    ~gs_shader();

    bool gs_shader_init(const gs_shader_info &info);

    GLuint obj();
    gs_shader_type type();

    const std::vector<shader_attrib> &gs_shader_attribs() const;
    const std::vector<std::shared_ptr<gs_shader_param>> &gs_shader_params() const;   
    std::shared_ptr<gs_shader_param> gs_shader_param_by_unit(int unit);
    const std::vector<std::shared_ptr<gs_sampler_state>> &gs_shader_samplers() const;

    void gs_shader_set_matrix4(const glm::mat4x4 &val);
    void gs_shader_set_matrix4(const std::shared_ptr<gs_shader_param> &param, const glm::mat4x4 &val);

private:
    std::string gl_get_shader_info(GLuint shader);
    bool gl_add_param(const gl_parser_shader_var &var, GLint *texture_id);
    bool gl_add_params(const std::vector<gl_parser_shader_var> &vars);
    bool gl_process_attribs(const std::vector<gl_parser_attrib> &attribs);
    bool gl_add_samplers(const std::vector<gl_parser_shader_sampler> &samplers);
    std::shared_ptr<gs_shader_param> gs_shader_get_param_by_name(const std::string &name);


private:
    std::unique_ptr<gs_shader_private> d_ptr{};
};
