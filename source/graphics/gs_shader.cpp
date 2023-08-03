#include "lite-obs/graphics/gs_shader.h"
#include "lite-obs/graphics/shaders.h"
#include "lite-obs/graphics/gl-helpers.h"

struct gs_sampler_info {
    gs_sample_filter filter;
    gs_address_mode address_u;
    gs_address_mode address_v;
    gs_address_mode address_w;
};

struct gs_shader_private
{
    gs_shader_type type{};
    GLuint obj{};

    std::shared_ptr<gs_shader_param> viewproj{};
    std::shared_ptr<gs_shader_param> world{};

    std::vector<shader_attrib> attribs{};
    std::vector<std::shared_ptr<gs_shader_param>> params{};

    std::vector<std::shared_ptr<gs_sampler_state>> samplers;

    ~gs_shader_private() {
        if (obj) {
            glDeleteShader(obj);
            gl_success("glDeleteShader");
        }
    }
};

static inline GLenum convert_shader_type(gs_shader_type type)
{
    switch (type) {
    case gs_shader_type::GS_SHADER_VERTEX:
        return GL_VERTEX_SHADER;
    case gs_shader_type::GS_SHADER_PIXEL:
        return GL_FRAGMENT_SHADER;
    }

    return GL_VERTEX_SHADER;
}

gs_shader::gs_shader()
{
    d_ptr = std::make_unique<gs_shader_private>();
}

gs_shader::~gs_shader()
{
    blog(LOG_DEBUG, "gs_shader destroyed.");
}

bool gs_shader::gs_shader_init(const gs_shader_info &info)
{
    d_ptr->type = info.type;
    GLenum type = convert_shader_type(info.type);

    int compiled = 0;
    bool success = true;

    d_ptr->obj = glCreateShader(type);
    if (!gl_success("glCreateShader") || !d_ptr->obj)
        return false;

#if defined WIN32
    std::string shader_str = "#version 150\n" + info.shader;
#else
    std::string shader_str = "#version 300 es\n" + info.shader;
#endif
    auto str = shader_str.data();
    glShaderSource(d_ptr->obj, 1, (const GLchar **)&str,
                   0);
    if (!gl_success("glShaderSource"))
        return false;

    glCompileShader(d_ptr->obj);
    if (!gl_success("glCompileShader"))
        return false;

#if 0
    blog(LOG_DEBUG, "+++++++++++++++++++++++++++++++++++");
    blog(LOG_DEBUG, "  GL shader string for: %s", file);
    blog(LOG_DEBUG, "-----------------------------------");
    blog(LOG_DEBUG, "%s", glsp->gl_string.array);
    blog(LOG_DEBUG, "+++++++++++++++++++++++++++++++++++");
#endif

    glGetShaderiv(d_ptr->obj, GL_COMPILE_STATUS, &compiled);
    if (!gl_success("glGetShaderiv"))
        return false;

    if (!compiled) {
        GLint infoLength = 0;
        glGetShaderiv(d_ptr->obj, GL_INFO_LOG_LENGTH, &infoLength);

        char *infoLog = (char *)malloc(sizeof(char) * infoLength);

        GLsizei returnedLength = 0;
        glGetShaderInfoLog(d_ptr->obj, infoLength, &returnedLength,
                           infoLog);
        blog(LOG_ERROR, "Error compiling shader:\n%s\n", infoLog);

        free(infoLog);

        success = false;
    }

    gl_get_shader_info(d_ptr->obj);

    if (success)
        success = gl_add_params(info.parser_shader_vars);
    /* Only vertex shaders actually require input attributes */
    if (success && d_ptr->type == gs_shader_type::GS_SHADER_VERTEX)
        success = gl_process_attribs(info.parser_attribs);
    if (success)
        gl_add_samplers(info.parser_shader_samplers);

    return success;
}

GLuint gs_shader::obj()
{
    return d_ptr->obj;
}

gs_shader_type gs_shader::type()
{
    return d_ptr->type;
}

const std::vector<shader_attrib> &gs_shader::gs_shader_attribs() const
{
    return d_ptr->attribs;
}

const std::vector<std::shared_ptr<gs_shader_param> > &gs_shader::gs_shader_params() const
{
    return d_ptr->params;
}

std::shared_ptr<gs_shader_param> gs_shader::gs_shader_param_by_unit(int unit)
{
    for (int i = 0; i < d_ptr->params.size(); ++i) {
        auto param = d_ptr->params[i];
        if (param->type == gs_shader_param_type::GS_SHADER_PARAM_TEXTURE) {
            if (param->texture_id == unit)
                return param;
        }
    }

    return nullptr;
}

const std::vector<std::shared_ptr<gs_sampler_state> > &gs_shader::gs_shader_samplers() const
{
    return d_ptr->samplers;
}

void gs_shader::gs_shader_set_matrix4(const glm::mat4x4 &val)
{
    gs_shader_set_matrix4(d_ptr->viewproj, val);
}

void gs_shader::gs_shader_set_matrix4(const std::shared_ptr<gs_shader_param> &param, const glm::mat4x4 &val)
{
    if (!param)
        return;

    param->cur_value.resize(sizeof(glm::mat4x4));
    memcpy(param->cur_value.data(), &val, sizeof(glm::mat4x4));
    param->changed = true;
}

std::string gs_shader::gl_get_shader_info(GLuint shader)
{
    std::string errors;
    GLint info_len = 0;
    GLsizei chars_written = 0;

    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
    if (!gl_success("glGetProgramiv") || !info_len)
        return errors;

    errors.resize(info_len + 1);
    glGetShaderInfoLog(shader, info_len, &chars_written, errors.data());
    gl_success("glGetShaderInfoLog");

    blog(LOG_DEBUG, "Compiler warnings/errors:\n%s", errors.c_str());

    return errors;
}

bool gs_shader::gl_add_param(const gl_parser_shader_var &var, GLint *texture_id)
{
    auto param = std::make_shared<gs_shader_param>();

    param->def_value.resize(var.default_val.size());
    param->cur_value.resize(var.default_val.size());

    param->name = var.name;
    param->type = get_shader_param_type(var.type.c_str());

    if (param->type == gs_shader_param_type::GS_SHADER_PARAM_TEXTURE) {
        param->sampler_id = var.gl_sampler_id;
        param->texture_id = (*texture_id)++;
    } else {
        param->changed = true;
    }

    param->def_value = var.default_val;
    param->cur_value = param->def_value;

    d_ptr->params.push_back(std::move(param));
    return true;
}

bool gs_shader::gl_add_params(const std::vector<gl_parser_shader_var> &vars)
{
    GLint tex_id = 0;

    for (int i = 0; i < vars.size(); i++)
        if (!gl_add_param(vars[i], &tex_id))
            return false;

    d_ptr->viewproj = gs_shader_get_param_by_name("ViewProj");
    d_ptr->world = gs_shader_get_param_by_name("World");

    return true;
}

static void get_attrib_type(const std::string mapping, attrib_type *type, size_t *index)
{
    if (mapping == "POSITION") {
        *type = attrib_type::ATTRIB_POSITION;

    } else if (mapping == "NORMAL") {
        *type = attrib_type::ATTRIB_NORMAL;

    } else if (mapping == "TANGENT") {
        *type = attrib_type::ATTRIB_TANGENT;

    } else if (mapping == "COLOR") {
        *type = attrib_type::ATTRIB_COLOR;

    } else if (strncmp(mapping.c_str(), "TEXCOORD", 8) == 0) {
        *type = attrib_type::ATTRIB_TEXCOORD;
        *index = mapping[8] - '0';
        return;

    } else if (mapping == "TARGET") {
        *type = attrib_type::ATTRIB_TARGET;
    }

    *index = 0;
}

bool gs_shader::gl_process_attribs(const std::vector<gl_parser_attrib> &attribs)
{
    for (int i = 0; i < attribs.size(); ++i) {
        const gl_parser_attrib &pa = attribs[i];
        shader_attrib attrib = {};

        /* don't parse output attributes */
        if (!pa.input)
            continue;

        get_attrib_type(pa.mapping, &attrib.type, &attrib.index);
        attrib.name = pa.name;

        d_ptr->attribs.push_back(std::move(attrib));
    }

    return true;
}

void shader_sampler_convert(const gl_parser_shader_sampler *ss, gs_sampler_info *info)
{
    size_t i;
    memset(info, 0, sizeof(struct gs_sampler_info));

    for (i = 0; i < ss->states.size(); i++) {
        std::string state = ss->states[i];
        std::string value = ss->values[i];

        if (astrcmpi(state.c_str(), "Filter") == 0)
            info->filter = get_sample_filter(value.c_str());
        else if (astrcmpi(state.c_str(), "AddressU") == 0)
            info->address_u = get_address_mode(value.c_str());
        else if (astrcmpi(state.c_str(), "AddressV") == 0)
            info->address_v = get_address_mode(value.c_str());
        else if (astrcmpi(state.c_str(), "AddressW") == 0)
            info->address_w = get_address_mode(value.c_str());
    }
}
bool gs_shader::gl_add_samplers(const std::vector<gl_parser_shader_sampler> &samplers)
{
    for (int i = 0; i < samplers.size(); ++i) {
        const gl_parser_shader_sampler &sampler = samplers[i];

        gs_sampler_info info;
        shader_sampler_convert(&sampler, &info);

        auto new_sampler = std::make_shared<gs_sampler_state>();

        convert_filter(info.filter, &new_sampler->min_filter, &new_sampler->mag_filter);
        new_sampler->address_u = convert_address_mode(info.address_u);
        new_sampler->address_v = convert_address_mode(info.address_v);
        new_sampler->address_w = convert_address_mode(info.address_w);

        d_ptr->samplers.push_back(std::move(new_sampler));
    }

    return true;
}

std::shared_ptr<gs_shader_param> gs_shader::gs_shader_get_param_by_name(const std::string &name)
{
    for (size_t i = 0; i < d_ptr->params.size(); i++) {
        auto param = d_ptr->params[i];

        if (param->name == name)
            return param;
    }

    return nullptr;
}
