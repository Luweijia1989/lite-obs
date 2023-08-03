#pragma once

#include <memory>
#include <vector>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>
#include "gs_subsystem_info.h"
#include "gs_shader_info.h"

struct program_param;
struct gs_program_private;
class gs_texture;
class gs_shader;
class gs_program
{
public:
    gs_program(const std::string &name);
    ~gs_program();

    bool gs_program_create(std::shared_ptr<gs_shader> vertex_shader, std::shared_ptr<gs_shader> pixel_shader);

    const std::string &gs_program_name();

    void gs_effect_set_texture(const char *name, std::shared_ptr<gs_texture> tex);
    void gs_effect_set_param(const char *name, float value);
    void gs_effect_set_param(const char *name, const glm::vec4 &value);
    void gs_effect_set_param(const char *name, const glm::vec2 &value);
    void gs_effect_set_param(const char *name, const void *value, size_t size);

    void gs_effect_upload_parameters(bool change_only);
    void gs_effect_clear_tex_params();
    void gs_effect_clear_all_params();

    std::shared_ptr<gs_shader> gs_effect_vertex_shader();
    std::shared_ptr<gs_shader> gs_effect_pixel_shader();

    GLuint gs_effect_obj();
    const std::vector<GLint> &gs_effect_attribs();

private:
    bool assign_program_attrib(const shader_attrib &attrib);
    bool assign_program_attribs();

    bool assign_program_param(const std::shared_ptr<gs_shader_param> &param);
    bool assign_program_shader_params(std::shared_ptr<gs_shader> shader);
    bool assign_program_params();

    program_param* gs_effect_get_param_by_name(const char *name);

private:
    std::unique_ptr<gs_program_private> d_ptr{};
};
