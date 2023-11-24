#pragma once

#include <memory>
#include "gs_subsystem.h"
#include "gs_subsystem_info.h"

struct gs_core_painter_private;

struct fbo_info;

class gs_vertexbuffer;
class gs_program;

class gs_core_render
{
public:
    gs_core_render();
    ~gs_core_render();

    bool gs_core_render_ready();

    void gs_core_render_draw_sprite(const std::shared_ptr<gs_program> &program, const std::shared_ptr<gs_texture> &src, const std::shared_ptr<gs_texture> &target, uint32_t flag, uint32_t width, uint32_t height, bool blend, std::function<void (glm::mat4x4 &)> mat_func);
    void gs_core_render_draw_convert(std::shared_ptr<gs_texture> tex, std::shared_ptr<gs_program> program);
    void gs_core_render_do_main_render_task(std::function<void ()> task, const std::shared_ptr<gs_texture> &target);

private:
    bool set_current_fbo(std::shared_ptr<fbo_info> fbo);
    uint32_t get_target_height();

    bool can_render(uint32_t num_verts);
    void update_viewproj_matrix();

    void clear_color(uint32_t clear_flags, glm::vec4 *color, float depth, uint8_t stencil);
    void enable_blending(bool enable);
    void push_blend_state();
    void pop_blend_state();
    void reset_blend_state();
    void set_blend_function_param(enum gs_blend_type src_c, enum gs_blend_type dest_c, enum gs_blend_type src_a, enum gs_blend_type dest_a);

    void enable_depth_test(bool enable);
    void enable_cull_face(bool enable);

    void set_viewport(int x, int y, int width, int height);

    void push_viewport();
    void pop_viewport();

    void push_projection();
    void pop_projection();

    void load_texture(std::weak_ptr<gs_texture> p_tex, int unit);

    void render_begin();
    void render_end();

    void render_matrix_begin();
    void render_matrix_end();

    bool set_render_target(const std::shared_ptr<gs_texture> &tex);
    void set_ortho(float left, float right, float top, float bottom, float near, float far);
    void render_target_begin(const std::shared_ptr<gs_texture> &tex);
    void render_target_end();

    void clear_textures();
    void load_default_pixelshader_samplers();
    void set_program(std::shared_ptr<gs_program> program);
    void draw_internal(gs_draw_mode draw_mode, uint32_t start_vert, uint32_t num_verts);
    void draw_sprite_internal(std::shared_ptr<gs_texture> tex, uint32_t flip, uint32_t width, uint32_t height);

private:
    std::unique_ptr<gs_core_painter_private> d_ptr{};
};
