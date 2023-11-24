#include "lite-obs/graphics/gs_subsystem.h"
#include "lite-obs/graphics/gs_core_render.h"
#include "lite-obs/graphics/gs_texture.h"
#include "lite-obs/graphics/gs_shader.h"
#include "lite-obs/graphics/shaders.h"
#include "lite-obs/graphics/gs_program.h"
#include "lite-obs/graphics/gs_context_gl.h"

#include "lite-obs/util/log.h"

#include <mutex>
#include <list>
#include <map>
#include <algorithm>
#include <glm/mat4x4.hpp>

std::vector<std::string> split (const std::string &s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

struct graphics_subsystem_private
{
    std::shared_ptr<gs_core_render> core_painter{};
    std::shared_ptr<gs_context_gl> gl_ctx{};

    std::mutex effect_mutex;
    std::map<std::string, std::shared_ptr<gs_program>> effects{};

    std::recursive_mutex mutex;
    std::atomic_long ref{};

    std::shared_ptr<gs_shader> shader_from_string(const std::string &shader_string, bool vertex_shader, std::string &out_name)
    {
        auto strings = split(shader_string, "---------------------------------------");
        if (strings.size() != 5)
            return nullptr;

        auto &shader_name = strings[0];
        auto &shader_source = strings[1];
        auto &shader_param = strings[2];
        auto &shader_attribs = strings[3];
        auto &shader_samplers = strings[4];

        gs_shader_info shader_info;
        shader_info.shader = shader_source;
        shader_info.type = vertex_shader ? gs_shader_type::GS_SHADER_VERTEX : gs_shader_type::GS_SHADER_PIXEL;

        if (shader_param.length() > 0) {
            auto params = split(shader_param, "+++++++++++++++++++++++++++++++++++++++");
            for (size_t i = 0; i < params.size(); ++i) {
                auto &param = params[i];
                auto p = split(param, " ");
                if (p.size() != 6)
                    return nullptr;

                gl_parser_shader_var var;
                var.type = p[0];
                var.name = p[1];
                var.mapping = p[2] == "null" ? "" : p[2];
                var.var_type = (shader_var_type)std::stoi(p[3]);
                var.gl_sampler_id = std::stoull(p[5]);

                shader_info.parser_shader_vars.push_back(std::move(var));
            }
        }

        if (shader_attribs.length() > 0) {
            auto attribs = split(shader_attribs, "+++++++++++++++++++++++++++++++++++++++");
            for (size_t i = 0; i < attribs.size(); ++i) {
                auto &attrib = attribs[i];
                auto a = split(attrib, " ");
                if (a.size() != 3)
                    return nullptr;

                gl_parser_attrib att;
                att.name = a[0];
                att.mapping = a[1] == "null" ? "" : a[1];
                att.input = std::stoi(a[2]);

                shader_info.parser_attribs.push_back(std::move(att));
            }
        }

        if (shader_samplers.length() > 0) {
            auto samplers = split(shader_samplers, "+++++++++++++++++++++++++++++++++++++++");
            for (size_t i = 0; i < samplers.size(); ++i) {
                auto name = samplers[i];
                if (name == "def_sampler") {
                    gl_parser_shader_sampler sampler;
                    sampler.name = "def_sampler";
                    sampler.states = {"Filter", "AddressU", "AddressV"};
                    sampler.values = {"Linear", "Clamp", "Clamp"};

                    shader_info.parser_shader_samplers.push_back(std::move(sampler));
                } else if (name == "textureSampler") {
                    gl_parser_shader_sampler sampler;
                    sampler.name = "textureSampler";
                    sampler.states = {"Filter", "AddressU", "AddressV"};
                    sampler.values = {"Linear", "Clamp", "Clamp"};

                    shader_info.parser_shader_samplers.push_back(std::move(sampler));
                }
            }
        }

        auto shader = std::make_shared<gs_shader>();
        if (!shader->gs_shader_init(shader_info))
            return nullptr;

        out_name = shader_name;
        return shader;
    }

    bool init_effect() {
        conversion_shaders_total.erase(std::remove(conversion_shaders_total.begin(), conversion_shaders_total.end(), '\n'), conversion_shaders_total.end());

        auto strs = split(conversion_shaders_total, "=======================================");
        for (size_t i = 0; i < strs.size(); i+=2) {
            auto &v = strs[i];
            auto &p = strs[i+1];
            std::string name;
            auto vertex_shader = shader_from_string(v, true, name);
            auto pixel_shader = shader_from_string(p, false, name);

            if (vertex_shader && pixel_shader) {
                auto program = std::make_shared<gs_program>(name);
                if (program->gs_program_create(vertex_shader, pixel_shader)) {
                    effects.insert({name, program});
                    blog(LOG_DEBUG, "gs program create: %s, obj id: %d.", name.c_str(), program->gs_effect_obj());
                } else {
                    blog(LOG_DEBUG, "effect %s init error!", name.c_str());
                    return false;
                }
            } else {
                blog(LOG_DEBUG, "create shader error.");
                return false;
            }
        }

        return true;
    }

    bool create(void *plat) {
        bool res = false;
        do {
            auto ctx = std::make_shared<gs_context_gl>(plat);
            if (!ctx->gs_context_ready())
                break;

            gl_ctx = ctx;

            auto painter = std::make_shared<gs_core_render>();
            if (!painter->gs_core_render_ready())
                break;

            core_painter = painter;

            if (!init_effect())
                break;

            res = true;
        } while (false);

        if (gl_ctx) {
            gl_ctx->done_current();
        }

        return res;
    }

    ~graphics_subsystem_private() {
        gl_ctx->make_current();
        effects.clear();
        core_painter.reset();
        gl_ctx->done_current();
        gl_ctx.reset();
    }
};

static thread_local graphics_subsystem *thread_graphics = NULL;

static bool gs_valid(const char *f)
{
    if (!thread_graphics) {
        blog(LOG_DEBUG, "%s: called while not in a graphics context",
             f);
        return false;
    }

    return true;
}

graphics_subsystem::graphics_subsystem()
{
    d_ptr = std::make_unique<graphics_subsystem_private>();
}

graphics_subsystem::~graphics_subsystem()
{
    while (thread_graphics)
        graphics_subsystem::done_current();

    thread_graphics = this;
    d_ptr.reset();
    thread_graphics = nullptr;

    blog(LOG_DEBUG, "graphics_subsystem destroyed.");
}

bool graphics_subsystem::texture_share_enabled()
{
    if (!d_ptr->gl_ctx)
        return false;

    return d_ptr->gl_ctx->gs_device_rc_texture_share_enabled();
}

void *graphics_subsystem::render_context()
{
    if (!d_ptr->gl_ctx)
        return nullptr;

    return d_ptr->gl_ctx->gs_device_platform_rc();
}

std::unique_ptr<graphics_subsystem> graphics_subsystem::gs_create_graphics_system(void *plat)
{
    gl_context_helper helper;
    auto gs = std::make_unique<graphics_subsystem>();
    if (!gs->d_ptr->create(plat))
        return nullptr;

    return gs;
}

void graphics_subsystem::make_current(const std::unique_ptr<graphics_subsystem> &graphics)
{
    if (!graphics)
        return;

    bool is_current = thread_graphics == graphics.get();
    if (thread_graphics && !is_current) {
        while (thread_graphics)
            graphics_subsystem::done_current();
    }

    if (!is_current) {
        graphics->d_ptr->mutex.lock();
        graphics->d_ptr->gl_ctx->make_current();
        thread_graphics = graphics.get();
    }

    graphics->d_ptr->ref++;
}

void graphics_subsystem::done_current(bool request_flush)
{
    if (!thread_graphics)
        return;

    if (request_flush)
        glFlush();

    if (gs_valid("graphics_subsystem::done_current")) {
        if (!--thread_graphics->d_ptr->ref) {
            graphics_subsystem *graphics = thread_graphics;

            graphics->d_ptr->gl_ctx->done_current();
            graphics->d_ptr->mutex.unlock();
            thread_graphics = nullptr;
        }
    }
}

std::shared_ptr<gs_program> graphics_subsystem::get_effect_by_name(const char *name)
{
    if (!gs_valid("graphics_subsystem::get_effect_by_name"))
        return nullptr;

    if (thread_graphics->d_ptr->effects.contains(name))
        return thread_graphics->d_ptr->effects.at(name);

    return nullptr;
}

void graphics_subsystem::process_main_render_task(std::function<void ()> task, const std::shared_ptr<gs_texture> &target)
{
    if (!gs_valid("graphics_subsystem::process_main_render_task"))
        return;

    thread_graphics->d_ptr->core_painter->gs_core_render_do_main_render_task(task, target);
}

void graphics_subsystem::draw_convert(std::shared_ptr<gs_texture> tex, std::shared_ptr<gs_program> program)
{
    if (!gs_valid("graphics_subsystem::draw_convert"))
        return;

    thread_graphics->d_ptr->core_painter->gs_core_render_draw_convert(tex, program);
}

void graphics_subsystem::draw_sprite(const std::shared_ptr<gs_program> &program, const std::shared_ptr<gs_texture> &src, const std::shared_ptr<gs_texture> &target, uint32_t flag, uint32_t width, uint32_t height, bool blend, std::function<void (glm::mat4x4 &)> mat_func)
{
    if (!gs_valid("graphics_subsystem::draw_sprite"))
        return;

    thread_graphics->d_ptr->core_painter->gs_core_render_draw_sprite(program, src, target, flag, width, height, blend, mat_func);
}
