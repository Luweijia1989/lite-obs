#pragma once

#include <memory>
#include <vector>
#include <stdint.h>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include "gs_subsystem_info.h"

struct gs_tvertarray {
    size_t width{};
    void *array{};
};

struct gs_vb_data {
    size_t num{};
    std::vector<glm::vec4> points{};
    std::vector<glm::vec4> normals{};
    std::vector<glm::vec4> tangents{};
    std::vector<uint32_t> colors{};

    size_t num_tex{};
    std::vector<gs_tvertarray> tvarray{};

    void assign_sprite_uv(float *start, float *end, bool flip) {
        if (!flip) {
            *start = 0.0f;
            *end = 1.0f;
        } else {
            *start = 1.0f;
            *end = 0.0f;
        }
    }

    void build_sprite(float fcx, float fcy, float start_u, float end_u, float start_v, float end_v) {
        auto arr = tvarray[0].array;
        glm::vec2 *tvarray = (glm::vec2 *)arr;

        points[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        points[1] = glm::vec4(fcx, 0.0f, 0.0f, 0.0f);
        points[2] = glm::vec4(0.0f, fcy, 0.0f, 0.0f);
        points[3] = glm::vec4(fcx, fcy, 0.0f, 0.0f);

        tvarray[0] = glm::vec2(start_u, start_v);


        tvarray[1] = glm::vec2(end_u, start_v);
        tvarray[2] = glm::vec2(start_u, end_v);
        tvarray[3] = glm::vec2(end_u, end_v);
    }

    void build_sprite_norm(float fcx, float fcy, uint32_t flip) {
        float start_u, end_u;
        float start_v, end_v;

        assign_sprite_uv(&start_u, &end_u, (flip & GS_FLIP_U) != 0);
        assign_sprite_uv(&start_v, &end_v, (flip & GS_FLIP_V) != 0);
        build_sprite(fcx, fcy, start_u, end_u, start_v, end_v);
    }

    ~gs_vb_data() {
        for (int i = 0; i < num_tex; ++i) {
            free(tvarray[i].array);
        }
    }
};

struct gs_vertexbuffer_private;
class gs_vertexbuffer
{
public:
    gs_vertexbuffer();
    ~gs_vertexbuffer();

    bool gs_vertexbuffer_init_sprite();

    std::shared_ptr<gs_vb_data> gs_vertexbuffer_get_data();

    void gs_vertexbuffer_flush();
    void gs_vertexbuffer_flush_direct(const gs_vb_data *data);

    bool gs_load_vb_buffers(attrib_type t, size_t index, GLuint id);

    GLuint gs_vertexbuffer_vao();

    size_t gs_vertexbuffer_num();

private:
    bool create_buffers();
    void gs_vertexbuffer_flush_internal(const gs_vb_data *data);
    GLuint get_vb_buffer(attrib_type type, size_t index, GLint *width, GLenum *gl_type);

private:
    std::unique_ptr<gs_vertexbuffer_private> d_ptr{};
};
