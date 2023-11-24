#include "lite-obs/graphics/gs_context_gl.h"
#include "lite-obs/util/log.h"

struct gs_device_rc_private
{
    void *plat{};
    bool texture_share_enabled{};
};

gs_context_gl::gs_context_gl(void *plat)
{
    d_ptr = std::make_unique<gs_device_rc_private>();
    d_ptr->plat = gl_platform_create(plat);
    if (!d_ptr->plat)
        blog(LOG_ERROR, "fail to create device rc");
}

gs_context_gl::~gs_context_gl()
{
    if (d_ptr->plat) {
        gl_platform_destroy(d_ptr->plat);
        d_ptr->plat = nullptr;
    }
}

bool gs_context_gl::gs_context_ready()
{
    return d_ptr->plat != nullptr;
}

void *gs_context_gl::gs_device_platform_rc()
{
    return get_device_context_internal(d_ptr->plat);
}

void gs_context_gl::make_current()
{
    if (!d_ptr->plat)
        return;

    device_enter_context_internal(d_ptr->plat);
}

void gs_context_gl::done_current()
{
    if (!d_ptr->plat)
        return;

    device_leave_context_internal(d_ptr->plat);
}

void gs_context_gl::set_texture_share_enabled(bool enabled)
{
    d_ptr->texture_share_enabled = enabled;
}

bool gs_context_gl::gs_device_rc_texture_share_enabled()
{
    return d_ptr->texture_share_enabled;
}
