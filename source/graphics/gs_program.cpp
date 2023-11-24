#include "lite-obs/graphics/gs_program.h"
#include "lite-obs/graphics/gs_subsystem_info.h"
#include "lite-obs/graphics/gl_helpers.h"
#include "lite-obs/util/log.h"
#include "lite-obs/graphics/gs_shader.h"
#include "lite-obs/graphics/gs_subsystem.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <vector>

struct program_param {
    GLint obj{};
    std::shared_ptr<gs_shader_param> param{};
};

struct gs_program_private
{
    std::string name{};
    GLuint obj{};
    std::shared_ptr<gs_shader> vertex_shader{};
    std::shared_ptr<gs_shader> pixel_shader{};

    std::vector<program_param> params{};
    std::vector<GLint> attribs{};

    ~gs_program_private() {
        if (obj) {
            glDeleteProgram(obj);
            gl_success("glDeleteProgram");
        }
    }
};

gs_program::gs_program(const std::string &name)
{
    d_ptr = std::make_unique<gs_program_private>();
    d_ptr->name = name;
}

gs_program::~gs_program()
{
    blog(LOG_DEBUG, "gs_program destroyed.");
}

static void print_link_errors(GLuint program)
{
    char *errors = NULL;
    GLint info_len = 0;
    GLsizei chars_written = 0;

    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
    if (!gl_success("glGetProgramiv") || !info_len)
        return;

    errors = (char *)calloc(1, info_len + 1);
    glGetProgramInfoLog(program, info_len, &chars_written, errors);
    gl_success("glGetShaderInfoLog");

    blog(LOG_DEBUG, "Linker warnings/errors:\n%s", errors);

    free(errors);
}

bool gs_program::gs_program_create(std::shared_ptr<gs_shader> vertex_shader, std::shared_ptr<gs_shader> pixel_shader)
{
    d_ptr->vertex_shader = vertex_shader;
    d_ptr->pixel_shader = pixel_shader;

    d_ptr->obj = glCreateProgram();
    if (!gl_success("glCreateProgram"))
        goto error_detach_neither;

    glAttachShader(d_ptr->obj, d_ptr->vertex_shader->obj());
    if (!gl_success("glAttachShader (vertex)"))
        goto error_detach_neither;

    glAttachShader(d_ptr->obj, d_ptr->pixel_shader->obj());
    if (!gl_success("glAttachShader (pixel)"))
        goto error_detach_vertex;

    glLinkProgram(d_ptr->obj);
    if (!gl_success("glLinkProgram")) {
        GLint linked = 0;
        glGetProgramiv(d_ptr->obj, GL_LINK_STATUS, &linked);
        if (!gl_success("glGetProgramiv"))
            goto error;

        if (!linked) {
            print_link_errors(d_ptr->obj);
            goto error;
        }
    }

    if (!assign_program_attribs())
        goto error;
    if (!assign_program_params())
        goto error;

    glDetachShader(d_ptr->obj, d_ptr->vertex_shader->obj());
    gl_success("glDetachShader (vertex)");

    glDetachShader(d_ptr->obj, d_ptr->pixel_shader->obj());
    gl_success("glDetachShader (pixel)");

    return true;

error:
    glDetachShader(d_ptr->obj, d_ptr->pixel_shader->obj());
    gl_success("glDetachShader (pixel)");

error_detach_vertex:
    glDetachShader(d_ptr->obj, d_ptr->vertex_shader->obj());
    gl_success("glDetachShader (vertex)");

error_detach_neither:
    return false;
}

const std::string &gs_program::gs_program_name()
{
    return d_ptr->name;
}

void gs_program::gs_effect_set_texture(const char *name, std::shared_ptr<gs_texture> tex)
{
    auto p = gs_effect_get_param_by_name(name);
    if (!p)
        return;

    p->param->texture = tex;
    p->param->changed = true;
}

void gs_program::gs_effect_set_param(const char *name, float value)
{
    auto p = gs_effect_get_param_by_name(name);
    if (!p)
        return;

    p->param->cur_value.resize(sizeof(float));
    memcpy(p->param->cur_value.data(), &value, sizeof(float));
    p->param->changed = true;
}

void gs_program::gs_effect_set_param(const char *name, const glm::vec4 &value)
{
    auto p = gs_effect_get_param_by_name(name);
    if (!p)
        return;

    p->param->cur_value.resize(sizeof(glm::vec4));
    memcpy(p->param->cur_value.data(), &value, sizeof(glm::vec4));
    p->param->changed = true;
}

void gs_program::gs_effect_set_param(const char *name, const glm::vec2 &value)
{
    auto p = gs_effect_get_param_by_name(name);
    if (!p)
        return;

    p->param->cur_value.resize(sizeof(glm::vec2));
    memcpy(p->param->cur_value.data(), &value, sizeof(glm::vec2));
    p->param->changed = true;
}

void gs_program::gs_effect_set_param(const char *name, const void *value, size_t size)
{
    auto p = gs_effect_get_param_by_name(name);
    if (!p)
        return;

    p->param->cur_value.resize(size);
    memcpy(p->param->cur_value.data(), value, size);
    p->param->changed = true;
}

static inline bool validate_param(const program_param &pp, size_t expected_size)
{
    if (pp.param->cur_value.size() != expected_size) {
        blog(LOG_ERROR,
             "Parameter '%s' set to invalid size %u, "
             "expected %u",
             pp.param->name.c_str(), (unsigned int)pp.param->cur_value.size(), (unsigned int)expected_size);
        return false;
    }

    return true;
}

void gs_program::gs_effect_upload_parameters(bool change_only, std::function<void(std::weak_ptr<gs_texture>, int)> texture_update)
{
    for (size_t i = 0; i < d_ptr->params.size(); ++i) {
        auto &param = d_ptr->params[i];
        if (change_only && !param.param->changed)
            continue;
        if (param.param->cur_value.empty() && !param.param->texture.lock()) {
            param.param->changed = false;
            continue;
        }

        void *array = param.param->cur_value.data();

        if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_BOOL ||
                param.param->type == gs_shader_param_type::GS_SHADER_PARAM_INT) {
            if (validate_param(param, sizeof(int))) {
                glUniform1iv(param.obj, 1, (int *)array);
                gl_success("glUniform1iv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_INT2) {
            if (validate_param(param, sizeof(int) * 2)) {
                glUniform2iv(param.obj, 1, (int *)array);
                gl_success("glUniform2iv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_INT3) {
            if (validate_param(param, sizeof(int) * 3)) {
                glUniform3iv(param.obj, 1, (int *)array);
                gl_success("glUniform3iv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_INT4) {
            if (validate_param(param, sizeof(int) * 4)) {
                glUniform4iv(param.obj, 1, (int *)array);
                gl_success("glUniform4iv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_FLOAT) {
            if (validate_param(param, sizeof(float))) {
                glUniform1fv(param.obj, 1, (float *)array);
                gl_success("glUniform1fv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_VEC2) {
            if (validate_param(param, sizeof(glm::vec2))) {
                glUniform2fv(param.obj, 1, (float *)array);
                gl_success("glUniform2fv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_VEC3) {
            if (validate_param(param, sizeof(float) * 3)) {
                glUniform3fv(param.obj, 1, (float *)array);
                gl_success("glUniform3fv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_VEC4) {
            if (validate_param(param, sizeof(glm::vec4))) {
                glUniform4fv(param.obj, 1, (float *)array);
                gl_success("glUniform4fv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_MATRIX4X4) {
            if (validate_param(param, sizeof(glm::mat4x4))) {
                glUniformMatrix4fv(param.obj, 1, false, (float *)array);
                gl_success("glUniformMatrix4fv");
            }
        } else if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_TEXTURE) {
            glUniform1i(param.obj, param.param->texture_id);
            texture_update(param.param->texture, param.param->texture_id);
        }

        param.param->changed = false;
    }
}

void gs_program::gs_effect_clear_tex_params()
{
    for (size_t i = 0; i < d_ptr->params.size(); ++i) {
        auto &param = d_ptr->params[i];
        if (param.param->type == gs_shader_param_type::GS_SHADER_PARAM_TEXTURE) {
            param.param->texture.reset();
        }
    }
}

void gs_program::gs_effect_clear_all_params()
{
    for (size_t i = 0; i < d_ptr->params.size(); ++i) {
        auto &param = d_ptr->params[i];
        param.param->cur_value.clear();
        param.param->changed = false;
    }
}

std::shared_ptr<gs_shader> gs_program::gs_effect_vertex_shader()
{
    return d_ptr->vertex_shader;
}

std::shared_ptr<gs_shader> gs_program::gs_effect_pixel_shader()
{
    return d_ptr->pixel_shader;
}

GLuint gs_program::gs_effect_obj()
{
    return d_ptr->obj;
}

const std::vector<GLint> &gs_program::gs_effect_attribs()
{
    return d_ptr->attribs;
}

bool gs_program::assign_program_attrib(const shader_attrib &attrib)
{
    GLint attrib_obj = glGetAttribLocation(d_ptr->obj, attrib.name.c_str());
    if (!gl_success("glGetAttribLocation"))
        return false;

    if (attrib_obj == -1) {
        blog(LOG_ERROR,
             "glGetAttribLocation: Could not find "
             "attribute '%s'",
             attrib.name.c_str());
        return false;
    }

    d_ptr->attribs.push_back(attrib_obj);
    return true;
}

bool gs_program::assign_program_attribs()
{
    auto attribs = d_ptr->vertex_shader->gs_shader_attribs();
    for (size_t i = 0; i < attribs.size(); i++) {
        auto attrib = attribs[i];
        if (!assign_program_attrib(attrib))
            return false;
    }

    return true;
}

bool gs_program::assign_program_param(const std::shared_ptr<gs_shader_param> &param)
{
    struct program_param info;

    info.obj = glGetUniformLocation(d_ptr->obj, param->name.c_str());
    if (!gl_success("glGetUniformLocation"))
        return false;

    if (info.obj == -1) {
        return true;
    }

    info.param = param;
    d_ptr->params.push_back(std::move(info));
    return true;
}

bool gs_program::assign_program_shader_params(std::shared_ptr<gs_shader> shader)
{
    auto params = shader->gs_shader_params();
    for (size_t i = 0; i < params.size(); i++) {
        auto param = params[i];
        if (!assign_program_param(param))
            return false;
    }

    return true;
}

bool gs_program::assign_program_params()
{
    if (!assign_program_shader_params(d_ptr->vertex_shader))
        return false;
    if (!assign_program_shader_params(d_ptr->pixel_shader))
        return false;

    return true;
}

program_param *gs_program::gs_effect_get_param_by_name(const char *name)
{
    for (size_t i = 0; i < d_ptr->params.size(); ++i) {
        program_param *p = &d_ptr->params[i];
        if (p->param->name == name)
            return p;
    }

    return nullptr;
}

