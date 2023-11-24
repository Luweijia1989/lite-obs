#include "lite-obs/graphics/gs_context_gl.h"
#include "lite-obs/graphics/gs_subsystem_info.h"
#include "lite-obs/util/log.h"

#if TARGET_PLATFORM == PLATFORM_MAC || TARGET_PLATFORM == PLATFORM_IOS

#include "lite-obs/graphics/gl_context_helpers_apple.h"

struct gl_platform
{
    void *ctx{nullptr};
    bool release_on_destroy = true;
    ~gl_platform() {
        if (release_on_destroy) {
          gl_destroy_context(ctx);
        }
    }
};

void *gs_context_gl::gs_create_platform_rc()
{
    return nullptr;
}

void gs_context_gl::gs_destroy_platform_rc(void *plat)
{
    (void)plat;
}

void *gs_context_gl::gl_platform_create(void *)
{
    auto pair = gl_create_context(gl_current_context());
    if (pair.second)
      set_texture_share_enabled(true);

    gl_make_current(pair.first);

    gl_platform *plat = new gl_platform;
    plat->ctx = pair.first;
    return plat;
}

gl_context_helper::gl_context_helper()
{
    platform = std::make_shared<gl_platform>();
    platform->release_on_destroy = false;
    platform->ctx = gl_current_context();
}

gl_context_helper::~gl_context_helper()
{
    if (platform->ctx) {
        gl_make_current(platform->ctx);
    }
}

void gs_context_gl::device_enter_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    gl_make_current(plat->ctx);
}

void gs_context_gl::device_leave_context_internal(void *param)
{
    (void)param;
    gl_done_current();
}

void gs_context_gl::gl_platform_destroy(void *plat)
{
    gl_platform *p = (gl_platform *)plat;
    delete p;
}

void *gs_context_gl::get_device_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    return plat->ctx;
}
#endif
