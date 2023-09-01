#include "lite-obs/graphics/gs_simple_texture_drawer.h"
#include "lite-obs/graphics/gl_helpers.h"
#include "lite-obs/util/log.h"
#include <string>

gs_simple_texture_drawer::gs_simple_texture_drawer()
{
    const char* vShaderCode = R"(
    #version 300 es
    precision mediump float;
    in vec3 aPos;
    in vec2 aTexCoord;
    out vec2 TexCoord;

    void main()
    {
        gl_Position = vec4(aPos, 1.0);
        TexCoord = vec2(aTexCoord.x, aTexCoord.y);
    }
    )";
    const char * fShaderCode = R"(
    #version 300 es
    precision mediump float;
    out vec4 FragColor;
    in vec2 TexCoord;
    uniform sampler2D texture1;

    void main()
    {
        FragColor = texture(texture1, TexCoord);
    }
    )";

    unsigned int vertex, fragment;

    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);

    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex);
    glAttachShader(shader_program, fragment);
    glLinkProgram(shader_program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

#if TARGET_PLATFORM == PLATFORM_ANDROID
    float vertices[] = {
        // positions                 // texture coords
        1.0f,  -1.0f, 0.0f,    1.0f, 1.0f,
        1.0f,  1.0f,  0.0f,    1.0f, 0.0f,
        -1.0f, 1.0f,  0.0f,    0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,    0.0f, 1.0f
    };
#else
    float vertices[] = {
        // positions                 // texture coords
        1.0f,  1.0f, 0.0f,    1.0f, 1.0f,
        1.0f,  -1.0f,  0.0f,  1.0f, 0.0f,
        -1.0f, -1.0f,  0.0f,  0.0f, 0.0f,
        -1.0f, 1.0f, 0.0f,    0.0f, 1.0f
    };
#endif
    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    auto pos = glGetAttribLocation(shader_program, "aPos");
    auto tex_coord = glGetAttribLocation(shader_program, "aTexCoord");

    // position attribute
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    // texture coord attribute
    glEnableVertexAttribArray(tex_coord);
    glVertexAttribPointer(tex_coord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glUseProgram(shader_program);
    glUniform1i(glGetUniformLocation(shader_program, "texture1"), 0);
}

gs_simple_texture_drawer::~gs_simple_texture_drawer()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shader_program);
}

void gs_simple_texture_drawer::draw_texture(int tex_id)
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_id);

    glUseProgram(shader_program);
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}
