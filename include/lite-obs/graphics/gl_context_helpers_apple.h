#pragma once

#include <utility>
std::pair<void *, bool> gl_create_context(void *share_ctx);
void gl_destroy_context(void *ctx);

void *gl_current_context();

void gl_make_current(void *ctx);
void gl_done_current();
