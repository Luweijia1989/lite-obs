#include "lite-obs/graphics/gs_core_render.h"
#include "lite-obs/graphics/gs_subsystem.h"
#include "lite-obs/util/log.h"

#include "lite-obs/graphics/gl_helpers.h"
#include "lite-obs/graphics/gs_texture.h"
#include "lite-obs/graphics/gs_program.h"
#include "lite-obs/graphics/gs_shader.h"
#include "lite-obs/graphics/gs_vertexbuffer.h"

#include <glm/mat4x4.hpp>

#include <list>

struct blend_state {
    bool enabled{};
    gs_blend_type src_c{};
    gs_blend_type dest_c{};
    gs_blend_type src_a{};
    gs_blend_type dest_a{};
};

struct gs_core_painter_private
{
    blend_state cur_blend_state{};
    std::list<blend_state> blend_state_stack{};

    GLuint empty_vao{}; // for format covert render
    std::shared_ptr<gs_vertexbuffer> sprite_buffer{}; // for regular texture render

    std::weak_ptr<gs_texture> prev_render_target{};
    std::weak_ptr<gs_texture> cur_render_target{};
    std::weak_ptr<fbo_info> cur_fbo{};

    std::vector<std::weak_ptr<gs_texture>> cur_textures;
    std::vector<std::weak_ptr<gs_sampler_state>> cur_samplers;

    std::weak_ptr<gs_vertexbuffer> cur_vertex_buffer{};

    std::weak_ptr<gs_program> cur_program{};

    gs_rect cur_viewport{};

    glm::mat4x4 cur_proj{0};
    glm::mat4x4 cur_viewproj{0};

    std::list<glm::mat4x4> proj_stack{};
    std::list<glm::mat4x4> matrix_stack{};
    std::list<gs_rect> viewport_stack{};

    gs_core_painter_private() {
        cur_textures.resize(GS_MAX_TEXTURES);
        cur_samplers.resize(GS_MAX_TEXTURES);

        glm::mat4x4 top_mat(1);
        matrix_stack.push_back(top_mat);
    }
};

gs_core_render::gs_core_render()
{
    d_ptr = std::make_unique<gs_core_painter_private>();

    blog(LOG_INFO, "---------------------------------");
    blog(LOG_INFO, "Initializing OpenGL...");

    const char *glVendor = (const char *)glGetString(GL_VENDOR);
    const char *glRenderer = (const char *)glGetString(GL_RENDERER);

    blog(LOG_INFO, "Loading up OpenGL on adapter %s %s", glVendor, glRenderer);

    const char *glVersion = (const char *)glGetString(GL_VERSION);
    const char *glShadingLanguage =
        (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

    blog(LOG_INFO, "OpenGL loaded successfully, version %s, shading " "language %s", glVersion, glShadingLanguage);

    set_blend_function_param(gs_blend_type::GS_BLEND_SRCALPHA,
                             gs_blend_type::GS_BLEND_INVSRCALPHA,
                             gs_blend_type::GS_BLEND_ONE,
                             gs_blend_type::GS_BLEND_INVSRCALPHA);
    d_ptr->cur_blend_state.enabled = true;

    enable_cull_face(true);
    gl_gen_vertex_arrays(1, &d_ptr->empty_vao);

    auto vb = std::make_shared<gs_vertexbuffer>();
    if (vb->gs_vertexbuffer_init_sprite())
        d_ptr->sprite_buffer = std::move(vb);
}

gs_core_render::~gs_core_render()
{
    if (d_ptr->empty_vao) {
        gl_delete_vertex_arrays(1, &d_ptr->empty_vao);
        d_ptr->empty_vao = 0;
    }

    if (d_ptr->sprite_buffer)
        d_ptr->sprite_buffer.reset();

    blog(LOG_DEBUG, "gs_core_render destroyed.");
}

bool gs_core_render::gs_core_render_ready()
{
    return d_ptr->sprite_buffer && d_ptr->empty_vao;
}

bool gs_core_render::set_current_fbo(std::shared_ptr<fbo_info> fbo)
{
    auto cur_fbo = d_ptr->cur_fbo.lock();
    if (cur_fbo != fbo) {
        GLuint fbo_obj = fbo ? fbo->fbo : 0;
        if (!gl_bind_framebuffer(GL_FRAMEBUFFER, fbo_obj))
            return false;

        if (cur_fbo) {
            cur_fbo->cur_render_target.reset();
        }
    }

    d_ptr->cur_fbo = fbo;
    return true;
}

uint32_t gs_core_render::get_target_height()
{
    auto render_target = d_ptr->cur_render_target.lock();
    if (!render_target) {
        blog(LOG_DEBUG, "get_target_height (GL): no cur target");
        return 0;
    }

    return render_target->gs_texture_get_height();
}

bool gs_core_render::can_render(uint32_t num_verts)
{
    if (!d_ptr->cur_vertex_buffer.lock() && (num_verts == 0)) {
        blog(LOG_ERROR, "No vertex buffer specified");
        return false;
    }

    if (!d_ptr->cur_render_target.lock()) {
        blog(LOG_ERROR, "No active swap chain or render target");
        return false;
    }

    return true;
}

void gs_core_render::update_viewproj_matrix()
{
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    auto vs = program->gs_effect_vertex_shader();
    glm::mat4x4 cur_proj{0};

    cur_proj = d_ptr->cur_proj;

    if (d_ptr->cur_fbo.lock()) {
        cur_proj[0][1] = -cur_proj[0][1];
        cur_proj[1][1] = -cur_proj[1][1];
        cur_proj[2][1] = -cur_proj[2][1];
        cur_proj[3][1] = -cur_proj[3][1];

        glFrontFace(GL_CW);
    } else {
        glFrontFace(GL_CCW);
    }

    gl_success("glFrontFace");

    d_ptr->cur_viewproj = cur_proj * d_ptr->matrix_stack.back();
    d_ptr->cur_viewproj = glm::transpose(d_ptr->cur_viewproj);

    vs->gs_shader_set_matrix4(d_ptr->cur_viewproj);
}

void gs_core_render::draw_internal(gs_draw_mode draw_mode, uint32_t start_vert, uint32_t num_verts)
{
    auto program = d_ptr->cur_program.lock();
    if (!program) {
        blog(LOG_DEBUG, "no program when call draw_internal.");
        return;
    }

    auto vb = d_ptr->cur_vertex_buffer.lock();

    GLenum topology = convert_gs_topology(draw_mode);

    if (!can_render(num_verts))
        goto fail;

    if (vb) {
        if (!gl_bind_vertex_array(vb->gs_vertexbuffer_vao()))
            goto fail;
        auto attribs = program->gs_effect_vertex_shader()->gs_shader_attribs();
        auto s_attribs = program->gs_effect_attribs();
        for (size_t i = 0; i < attribs.size(); ++i) {
            vb->gs_load_vb_buffers(attribs[i].type, attribs[i].index, s_attribs[i]);
        }
    } else
        gl_bind_vertex_array(d_ptr->empty_vao);


    update_viewproj_matrix();

    program->gs_effect_upload_parameters(true, [this](std::weak_ptr<gs_texture> tex, int id){
        load_texture(tex, id);
    });

    if (num_verts == 0)
        num_verts = (uint32_t)vb->gs_vertexbuffer_num();
    glDrawArrays(topology, start_vert, num_verts);
    if (!gl_success("glDrawArrays"))
        goto fail;

    return;

fail:
    blog(LOG_ERROR, "device_draw (GL) failed");
}

void gs_core_render::load_texture(std::weak_ptr<gs_texture> p_tex, int unit)
{
    std::shared_ptr<gs_sampler_state> sampler;
    std::shared_ptr<gs_program> program;
    std::shared_ptr<gs_shader_param> param;
    auto cur_tex = d_ptr->cur_textures[unit].lock();
    auto tex = p_tex.lock();
    if (cur_tex == tex)
        goto fail;

    if (!gl_active_texture(GL_TEXTURE0 + unit))
        goto fail;

    /* the target for the previous text may not be the same as the
         * next texture, so unbind the previous texture first to be safe */
    if (cur_tex && (!tex || cur_tex->gs_texture_target() != tex->gs_texture_target()))
        gl_bind_texture(cur_tex->gs_texture_target(), 0);

    d_ptr->cur_textures[unit] = p_tex;

    program = d_ptr->cur_program.lock();
    if (!program)
        goto fail;

    param = program->gs_effect_pixel_shader()->gs_shader_param_by_unit(unit);
    if (!param)
        goto fail;

    if (!tex)
        goto fail;

    // texelFetch doesn't need a sampler
    if (param->sampler_id != (size_t)-1)
        sampler = d_ptr->cur_samplers[param->sampler_id].lock();
    else
        sampler = nullptr;

    if (!gl_bind_texture(tex->gs_texture_target(), tex->gs_texture_obj()))
        goto fail;
    if (sampler && !tex->gs_texture_load_texture_sampler(sampler))
        goto fail;

    return;

fail:
    blog(LOG_ERROR, "device_load_texture (GL) failed");
}

void gs_core_render::clear_textures()
{
    GLenum i;
    for (i = 0; i < GS_MAX_TEXTURES; i++) {
        auto tex = d_ptr->cur_textures[i].lock();
        if (tex) {
            gl_active_texture(GL_TEXTURE0 + i);
            gl_bind_texture(tex->gs_texture_target(), 0);
            d_ptr->cur_textures[i].reset();
        }
    }
}

void gs_core_render::load_default_pixelshader_samplers()
{
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    auto samplers = program->gs_effect_pixel_shader()->gs_shader_samplers();
    size_t i = 0;
    for (; i < samplers.size(); ++i) {
        d_ptr->cur_samplers[i] = samplers[i];
    }

    for (; i < GS_MAX_TEXTURES; i++)
        d_ptr->cur_samplers[i].reset();
}

void gs_core_render::draw_sprite_internal(std::shared_ptr<gs_texture> tex, uint32_t flip, uint32_t width, uint32_t height)
{
    if (!tex && (!width || !height)) {
        blog(LOG_ERROR, "A sprite cannot be drawn without a width/height");
        return;
    }

    float fcx = width ? (float)width : (float)tex->gs_texture_get_width();
    float fcy = height ? (float)height : (float)tex->gs_texture_get_height();

    auto data = d_ptr->sprite_buffer->gs_vertexbuffer_get_data();
    data->build_sprite_norm(fcx, fcy, flip);
    d_ptr->sprite_buffer->gs_vertexbuffer_flush();
    d_ptr->cur_vertex_buffer = d_ptr->sprite_buffer;

    draw_internal(gs_draw_mode::GS_TRISTRIP, 0, 0);
}

void gs_core_render::render_begin()
{
    clear_textures();
    load_default_pixelshader_samplers();
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    program->gs_effect_upload_parameters(false, [this](std::weak_ptr<gs_texture> tex, int id){
        load_texture(tex, id);
    });
}

void gs_core_render::render_end()
{
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    program->gs_effect_clear_tex_params();
    program->gs_effect_clear_all_params();
    clear_textures();
    set_program(nullptr);
}

void gs_core_render::render_matrix_begin()
{
    d_ptr->matrix_stack.emplace_back(d_ptr->matrix_stack.back());
    auto &mat = d_ptr->matrix_stack.back();
    mat = glm::mat4x4{1};
}

void gs_core_render::render_matrix_end()
{
    auto &stack = d_ptr->matrix_stack;
    if (stack.empty()) {
        blog(LOG_ERROR, "Tried to pop last matrix on stack");
        return;
    }

    stack.pop_back();
}

void gs_core_render::clear_color(uint32_t clear_flags, glm::vec4 *color, float depth, uint8_t stencil)
{
    GLbitfield gl_flags = 0;

    if (clear_flags & GS_CLEAR_COLOR) {
        glClearColor(color->x, color->y, color->z, color->w);
        gl_flags |= GL_COLOR_BUFFER_BIT;
    }

    if (clear_flags & GS_CLEAR_DEPTH) {
#ifdef PLATFORM_MOBILE
        glClearDepthf(depth);
#else
        glClearDepth(depth);
#endif
        gl_flags |= GL_DEPTH_BUFFER_BIT;
    }

    if (clear_flags & GS_CLEAR_STENCIL) {
        glClearStencil(stencil);
        gl_flags |= GL_STENCIL_BUFFER_BIT;
    }

    glClear(gl_flags);
    if (!gl_success("glClear"))
        blog(LOG_ERROR, "device_clear (GL) failed");
}

void gs_core_render::enable_blending(bool enable)
{
    d_ptr->cur_blend_state.enabled = enable;

    if (enable)
        gl_enable(GL_BLEND);
    else
        gl_disable(GL_BLEND);
}

void gs_core_render::push_blend_state()
{
    d_ptr->blend_state_stack.push_back(d_ptr->cur_blend_state);
}

void gs_core_render::pop_blend_state()
{
    if (d_ptr->blend_state_stack.empty())
        return;

    auto state = d_ptr->blend_state_stack.back();
    enable_blending(state.enabled);
    set_blend_function_param(state.src_c, state.dest_c, state.src_a, state.dest_a);

    d_ptr->blend_state_stack.pop_back();
}

void gs_core_render::reset_blend_state()
{
    if (!d_ptr->cur_blend_state.enabled)
        enable_blending(true);

    if (d_ptr->cur_blend_state.src_c != gs_blend_type::GS_BLEND_SRCALPHA ||
        d_ptr->cur_blend_state.dest_c != gs_blend_type::GS_BLEND_INVSRCALPHA ||
        d_ptr->cur_blend_state.src_a != gs_blend_type::GS_BLEND_ONE ||
        d_ptr->cur_blend_state.dest_a != gs_blend_type::GS_BLEND_INVSRCALPHA)
        set_blend_function_param(gs_blend_type::GS_BLEND_SRCALPHA, gs_blend_type::GS_BLEND_INVSRCALPHA, gs_blend_type::GS_BLEND_ONE, gs_blend_type::GS_BLEND_INVSRCALPHA);
}

void gs_core_render::set_blend_function_param(enum gs_blend_type src_c, enum gs_blend_type dest_c, enum gs_blend_type src_a, enum gs_blend_type dest_a)
{
    d_ptr->cur_blend_state.src_c = src_c;
    d_ptr->cur_blend_state.dest_c = dest_c;
    d_ptr->cur_blend_state.src_a = src_a;
    d_ptr->cur_blend_state.dest_a = dest_a;

    GLenum gl_src_c = convert_gs_blend_type(src_c);
    GLenum gl_dst_c = convert_gs_blend_type(dest_c);
    GLenum gl_src_a = convert_gs_blend_type(src_a);
    GLenum gl_dst_a = convert_gs_blend_type(dest_a);

    glBlendFuncSeparate(gl_src_c, gl_dst_c, gl_src_a, gl_dst_a);
    if (!gl_success("glBlendFuncSeparate"))
        blog(LOG_ERROR, "device_blend_function_separate (GL) failed");
}

void gs_core_render::enable_depth_test(bool enable)
{
    if (enable)
        gl_enable(GL_DEPTH_TEST);
    else
        gl_disable(GL_DEPTH_TEST);
}

void gs_core_render::enable_cull_face(bool enable)
{
    if (enable)
        gl_enable(GL_CULL_FACE);
    else
        gl_disable(GL_CULL_FACE);
}

void gs_core_render::set_viewport(int x, int y, int width, int height)
{
    /* GL uses bottom-up coordinates for viewports.  We want top-down */
    uint32_t base_height = get_target_height();
    int gl_y = 0;

    if (base_height)
        gl_y = base_height - y - height;

    glViewport(x, gl_y, width, height);
    if (!gl_success("glViewport"))
        blog(LOG_ERROR, "device_set_viewport (GL) failed");

    d_ptr->cur_viewport.x = x;
    d_ptr->cur_viewport.y = y;
    d_ptr->cur_viewport.cx = width;
    d_ptr->cur_viewport.cy = height;
}

void gs_core_render::push_viewport()
{
    d_ptr->viewport_stack.push_back(d_ptr->cur_viewport);
}

void gs_core_render::pop_viewport()
{
    auto &viewport = d_ptr->viewport_stack;
    if (viewport.empty())
        return;

    auto rect = viewport.back();
    viewport.pop_back();
    set_viewport(rect.x, rect.y, rect.cx, rect.cy);
}

void gs_core_render::push_projection()
{
    d_ptr->proj_stack.push_back(d_ptr->cur_proj);
}

void gs_core_render::pop_projection()
{
    if (d_ptr->proj_stack.empty())
        return;

    d_ptr->cur_proj = d_ptr->proj_stack.back();
    d_ptr->proj_stack.pop_back();
}

bool gs_core_render::set_render_target(const std::shared_ptr<gs_texture> &tex)
{
    auto cur_render_target = d_ptr->cur_render_target.lock();

    if (cur_render_target == tex)
        return true;

    d_ptr->cur_render_target = tex;

    if (!tex) {
        return set_current_fbo(nullptr);
    }

    auto fbo = tex->get_fbo();
    if (!fbo)
        return false;

    set_current_fbo(fbo);

    if (!fbo->attach_rendertarget(tex))
        return false;

    return true;
}

void gs_core_render::set_ortho(float left, float right, float top, float bottom, float znear, float zfar)
{
    auto &dst = d_ptr->cur_proj;
    memset(&dst, 0, sizeof(glm::mat4x4));

    float rml = right - left;
    float bmt = bottom - top;
    float fmn = zfar - znear;

    dst[0][0] = 2.0f / rml;
    dst[3][0] = (left + right) / -rml;

    dst[1][1] = 2.0f / -bmt;
    dst[3][1] = (bottom + top) / bmt;

    dst[2][2] = -2.0f / fmn;
    dst[3][2] = (zfar + znear) / -fmn;

    dst[3][3] = 1.0f;
}

void gs_core_render::render_target_begin(const std::shared_ptr<gs_texture> &tex)
{
    if (d_ptr->cur_render_target.lock()) {
        push_viewport();
        push_projection();
    }

    auto w = tex->gs_texture_get_width();
    auto h = tex->gs_texture_get_height();

    d_ptr->prev_render_target = d_ptr->cur_render_target;
    set_render_target(tex);

    glm::vec4 color(0);
    clear_color(GS_CLEAR_COLOR, &color, 1.0f, 0);

    set_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
    set_viewport(0, 0, w, h);
}

void gs_core_render::render_target_end()
{
    set_render_target(d_ptr->prev_render_target.lock());

    if (!d_ptr->cur_render_target.lock())
        return;

    pop_projection();
    pop_viewport();
}

void gs_core_render::set_program(std::shared_ptr<gs_program> program)
{
    std::shared_ptr<gs_program> cur_program = d_ptr->cur_program.lock();

    if (program != cur_program && cur_program) {
        glUseProgram(0);
        gl_success("glUseProgram (zero)");
    }

    if (program != cur_program) {
        d_ptr->cur_program = program;
        if (program) {
            glUseProgram(program->gs_effect_obj());
            gl_success("glUseProgram");
        }
    }
}

void gs_core_render::gs_core_render_draw_convert(std::shared_ptr<gs_texture> tex, std::shared_ptr<gs_program> program)
{
    if (!tex || !program)
        return;

    enable_blending(false);

    set_program(program);

    render_matrix_begin();
    render_target_begin(tex);
    render_begin();

    draw_internal(gs_draw_mode::GS_TRIS, 0, 3);

    render_end();
    render_target_end();
    render_matrix_end();

    enable_blending(true);
}

void gs_core_render::gs_core_render_do_main_render_task(std::function<void ()> task, const std::shared_ptr<gs_texture> &target)
{
    clear_textures();

    enable_depth_test(false);
    enable_cull_face(false);

    render_target_begin(target);

    push_blend_state();
    reset_blend_state();

    task();

    pop_blend_state();

    render_target_end();

    enable_blending(true);
}

void gs_core_render::gs_core_render_draw_sprite(const std::shared_ptr<gs_program> &program, const std::shared_ptr<gs_texture> &src, const std::shared_ptr<gs_texture> &target, uint32_t flag, uint32_t width, uint32_t height, bool blend, std::function<void (glm::mat4x4 &)> mat_func)
{
    if (!program)
        return;

    set_program(program);

    if (!blend)
        enable_blending(false);

    render_matrix_begin();

    if (target)
        render_target_begin(target);

    render_begin();

    auto &stack = d_ptr->matrix_stack;
    if (mat_func && !stack.empty()) {
        auto &top_mat = stack.back();
        mat_func(top_mat);
    }

    draw_sprite_internal(src, flag, width, height);

    render_end();

    if (target)
        render_target_end();

    render_matrix_end();

    enable_blending(true);
}
