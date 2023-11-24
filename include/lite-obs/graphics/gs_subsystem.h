#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext.hpp>

struct graphics_subsystem_private;
class gs_core_render;
class gs_texture;
class gs_program;
class gs_shader;
class graphics_subsystem
{
public:
    graphics_subsystem();
    ~graphics_subsystem();

    static std::unique_ptr<graphics_subsystem> gs_create_graphics_system(void *plat);

    static void make_current(const std::unique_ptr<graphics_subsystem> &graphics);
    static void done_current(bool request_flush = false);

    static std::shared_ptr<gs_program> get_effect_by_name(const char *name);

    static void process_main_render_task(std::function<void ()> task, const std::shared_ptr<gs_texture> &target);
    static void draw_sprite(const std::shared_ptr<gs_program> &program, const std::shared_ptr<gs_texture> &src, const std::shared_ptr<gs_texture> &target, uint32_t flag, uint32_t width, uint32_t height, bool blend, std::function<void (glm::mat4x4 &)> mat_func);
    static void draw_convert(std::shared_ptr<gs_texture> tex, std::shared_ptr<gs_program> program);

    bool texture_share_enabled();
    void *render_context();

public:
    std::unique_ptr<graphics_subsystem_private> d_ptr{};
};


