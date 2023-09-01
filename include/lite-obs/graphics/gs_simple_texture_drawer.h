#pragma once

#include "gs_subsystem_info.h"

class gs_simple_texture_drawer
{
public:
    gs_simple_texture_drawer();
    ~gs_simple_texture_drawer();
    void draw_texture(int tex_id);
private:
    GLuint VBO, VAO, EBO;
    GLuint shader_program{};
};
