#include "lite-obs/graphics/gs_device.h"
#include "lite-obs/graphics/gs_subsystem.h"
#include "lite-obs/util/log.h"

#include "lite-obs/graphics/gl-helpers.h"
#include "lite-obs/graphics/gs_texture.h"
#include "lite-obs/graphics/gs_program.h"
#include "lite-obs/graphics/gs_shader.h"
#include "lite-obs/graphics/gs_vertexbuffer.h"

#include <glm/mat4x4.hpp>

#include <list>

struct gs_device_private
{
    void *plat{};

    GLuint empty_vao{};

    std::weak_ptr<gs_texture> cur_render_target{};
    std::weak_ptr<gs_zstencil_buffer> cur_zstencil_buffer{};
    std::weak_ptr<fbo_info> cur_fbo{};

    std::vector<std::weak_ptr<gs_texture>> cur_textures;
    std::vector<std::weak_ptr<gs_sampler_state>> cur_samplers;

    std::weak_ptr<gs_vertexbuffer> cur_vertex_buffer{};
    std::weak_ptr<gs_indexbuffer> cur_index_buffer{};

    std::weak_ptr<gs_program> cur_program{};

    gs_cull_mode cur_cull_mode{};
    struct gs_rect cur_viewport{};

    glm::mat4x4 cur_proj{0};
    glm::mat4x4 cur_view{0};
    glm::mat4x4 cur_viewproj{0};

    std::list<glm::mat4x4> proj_stack{};

    gs_device_private() {
        cur_textures.resize(GS_MAX_TEXTURES);
        cur_samplers.resize(GS_MAX_TEXTURES);
    }
};

gs_device::gs_device()
{
    d_ptr = std::make_unique<gs_device_private>();
}

gs_device::~gs_device()
{
    if (d_ptr->plat) {
        gl_platform_destroy(d_ptr->plat);
        d_ptr->plat = nullptr;
    }

    blog(LOG_DEBUG, "gs_device destroyed.");
}

int gs_device::device_create(void *plat)
{
    blog(LOG_INFO, "---------------------------------");
    blog(LOG_INFO, "Initializing OpenGL...");

    d_ptr->plat = gl_platform_create(plat);
    if (!d_ptr->plat)
        return GS_ERROR_FAIL;

    const char *glVendor = (const char *)glGetString(GL_VENDOR);
    const char *glRenderer = (const char *)glGetString(GL_RENDERER);

    blog(LOG_INFO, "Loading up OpenGL on adapter %s %s", glVendor, glRenderer);

    const char *glVersion = (const char *)glGetString(GL_VERSION);
    const char *glShadingLanguage =
            (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

    blog(LOG_INFO, "OpenGL loaded successfully, version %s, shading " "language %s", glVersion, glShadingLanguage);

    gl_enable(GL_CULL_FACE);
    gl_gen_vertex_arrays(1, &d_ptr->empty_vao);

    device_leave_context();

    return GS_SUCCESS;
}

void gs_device::device_destroy()
{
    if (d_ptr->empty_vao) {
        gl_delete_vertex_arrays(1, &d_ptr->empty_vao);
        d_ptr->empty_vao = 0;
    }
}

void gs_device::device_enter_context()
{
    if (!d_ptr->plat)
        return;

    device_enter_context_internal(d_ptr->plat);
}

void gs_device::device_leave_context()
{
    if (!d_ptr->plat)
        return;

    device_leave_context_internal(d_ptr->plat);
}

void gs_device::device_blend_function_separate(gs_blend_type src_c, gs_blend_type dest_c, gs_blend_type src_a, gs_blend_type dest_a)
{
    GLenum gl_src_c = convert_gs_blend_type(src_c);
    GLenum gl_dst_c = convert_gs_blend_type(dest_c);
    GLenum gl_src_a = convert_gs_blend_type(src_a);
    GLenum gl_dst_a = convert_gs_blend_type(dest_a);

    glBlendFuncSeparate(gl_src_c, gl_dst_c, gl_src_a, gl_dst_a);
    if (!gl_success("glBlendFuncSeparate"))
        blog(LOG_ERROR, "device_blend_function_separate (GL) failed");
}


bool gs_device::set_current_fbo(std::shared_ptr<fbo_info> fbo)
{
    auto cur_fbo = d_ptr->cur_fbo.lock();
    if (cur_fbo != fbo) {
        GLuint fbo_obj = fbo ? fbo->fbo : 0;
        if (!gl_bind_framebuffer(GL_FRAMEBUFFER, fbo_obj))
            return false;

        if (cur_fbo) {
            cur_fbo->cur_render_target.reset();
            cur_fbo->cur_zstencil_buffer.reset();
        }
    }

    d_ptr->cur_fbo = fbo;
    return true;
}

uint32_t gs_device::get_target_height()
{
    auto render_target = d_ptr->cur_render_target.lock();
    if (!render_target) {
        blog(LOG_DEBUG, "get_target_height (GL): no cur target");
        return 0;
    }

    return render_target->gs_texture_get_height();
}

bool gs_device::can_render(uint32_t num_verts)
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

void gs_device::update_viewproj_matrix()
{
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    auto vs = program->gs_effect_vertex_shader();
    glm::mat4x4 cur_proj{0};

    gs_matrix_get(d_ptr->cur_view);
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

    d_ptr->cur_viewproj = d_ptr->cur_view * cur_proj;
    d_ptr->cur_viewproj = glm::transpose(d_ptr->cur_viewproj);

    vs->gs_shader_set_matrix4(d_ptr->cur_viewproj);
}

bool gs_device::gs_device_set_render_target(std::shared_ptr<gs_texture> tex, std::shared_ptr<gs_zstencil_buffer> zs)
{
    auto cur_render_target = d_ptr->cur_render_target.lock();
    auto cur_zstencil_buffer = d_ptr->cur_zstencil_buffer.lock();

    if (cur_render_target == tex && cur_zstencil_buffer == zs)
        return true;

    d_ptr->cur_render_target = tex;
    d_ptr->cur_zstencil_buffer = zs;

    if (!tex) {
        return set_current_fbo(nullptr);
    }

    auto fbo = tex->get_fbo();
    if (!fbo)
        return false;

    set_current_fbo(fbo);

    if (!fbo->attach_rendertarget(tex))
        return false;
    if (!fbo->attach_zstencil(zs))
        return false;

    return true;
}

void gs_device::gs_device_set_cull_mode(gs_cull_mode mode)
{
    if (d_ptr->cur_cull_mode == mode)
        return;

    if (d_ptr->cur_cull_mode == gs_cull_mode::GS_NEITHER)
        gl_enable(GL_CULL_FACE);

    d_ptr->cur_cull_mode = mode;

    if (mode == gs_cull_mode::GS_BACK)
        gl_cull_face(GL_BACK);
    else if (mode == gs_cull_mode::GS_FRONT)
        gl_cull_face(GL_FRONT);
    else
        gl_disable(GL_CULL_FACE);
}

void gs_device::gs_device_ortho(float left, float right, float top, float bottom, float znear, float zfar)
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

void gs_device::gs_device_set_viewport(int x, int y, int width, int height)
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

void gs_device::gs_device_get_viewport(gs_rect &rect)
{
    rect = d_ptr->cur_viewport;
}

void gs_device::gs_device_projection_push()
{
    d_ptr->proj_stack.push_back(d_ptr->cur_proj);
}

void gs_device::gs_device_projection_pop()
{
    if (d_ptr->proj_stack.empty())
        return;

    d_ptr->cur_proj = d_ptr->proj_stack.back();
    d_ptr->proj_stack.pop_back();
}

std::shared_ptr<gs_texture> gs_device::gs_device_get_render_target()
{
    return d_ptr->cur_render_target.lock();
}

std::shared_ptr<gs_zstencil_buffer> gs_device::gs_device_get_zstencil_target()
{
    return d_ptr->cur_zstencil_buffer.lock();
}

void gs_device::gs_device_load_vertexbuffer(std::shared_ptr<gs_vertexbuffer> vb)
{
    d_ptr->cur_vertex_buffer = vb;
}

void gs_device::gs_device_load_indexbuffer(std::shared_ptr<gs_indexbuffer> ib)
{
    d_ptr->cur_index_buffer = ib;
}

void gs_device::gs_device_set_program(std::shared_ptr<gs_program> program)
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

std::shared_ptr<gs_program> gs_device::gs_device_program()
{
    return d_ptr->cur_program.lock();
}

void gs_device::gs_device_draw(gs_draw_mode draw_mode, uint32_t start_vert, uint32_t num_verts)
{
    auto program = d_ptr->cur_program.lock();
    if (!program) {
        blog(LOG_DEBUG, "no program when call gs_device_draw.");
        return;
    }

    auto vb = d_ptr->cur_vertex_buffer.lock();
    auto ib = d_ptr->cur_index_buffer.lock();

    GLenum topology = convert_gs_topology(draw_mode);

    if (!can_render(num_verts))
        goto fail;

    if (vb) {
        if (!gl_bind_vertex_array(vb->gs_vertexbuffer_vao()))
            goto fail;
        auto attribs = program->gs_effect_vertex_shader()->gs_shader_attribs();
        auto s_attribs = program->gs_effect_attribs();
        for (int i = 0; i < attribs.size(); ++i) {
            vb->gs_load_vb_buffers(attribs[i].type, attribs[i].index, s_attribs[i]);
        }
        // todo indexbuffer
        //        if (ib && !gl_bind_buffer(GL_ELEMENT_ARRAY_BUFFER, ib->buffer))
        //            goto fail;
    } else
        gl_bind_vertex_array(d_ptr->empty_vao);


    update_viewproj_matrix();

    program->gs_effect_upload_parameters(true);

    if (ib) { // todo indexbuffer
        //        if (num_verts == 0)
        //            num_verts = (uint32_t)device->cur_index_buffer->num;
        //        glDrawElements(topology, num_verts, ib->gl_type,
        //                       (const GLvoid *)(start_vert * ib->width));
        //        if (!gl_success("glDrawElements"))
        //            goto fail;

    } else {
        if (num_verts == 0)
            num_verts = (uint32_t)vb->gs_vertexbuffer_num();
        glDrawArrays(topology, start_vert, num_verts);
        if (!gl_success("glDrawArrays"))
            goto fail;
    }

    return;

fail:
    blog(LOG_ERROR, "device_draw (GL) failed");
}

void gs_device::gs_device_load_texture(std::weak_ptr<gs_texture> p_tex, int unit)
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

void gs_device::gs_device_clear_textures()
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

void gs_device::gs_device_load_default_pixelshader_samplers()
{
    auto program = d_ptr->cur_program.lock();
    if (!program)
        return;

    auto samplers = program->gs_effect_pixel_shader()->gs_shader_samplers();
    int i = 0;
    for (; i < samplers.size(); ++i) {
        d_ptr->cur_samplers[i] = samplers[i];
    }

    for (; i < GS_MAX_TEXTURES; i++)
        d_ptr->cur_samplers[i].reset();
}

std::shared_ptr<gs_device> gs_create_device(void *plat)
{
    auto device = std::make_shared<gs_device>();
    if (device->device_create(plat) != GS_SUCCESS)
        return nullptr;

    return device;
}
