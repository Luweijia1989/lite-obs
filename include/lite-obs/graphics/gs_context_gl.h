#pragma once

#include <memory>

struct gl_platform;
class gl_context_helper
{
public:
    gl_context_helper();
    ~gl_context_helper();

private:
    std::shared_ptr<gl_platform> platform{};
};

struct gs_device_rc_private;
class gs_context_gl
{
public:
    static void *gs_create_platform_rc();
    static void gs_destroy_platform_rc(void *plat);

    gs_context_gl(void *plat);
    ~gs_context_gl();
    bool gs_context_ready();

    void *gs_device_platform_rc();
    bool gs_device_rc_texture_share_enabled();

    void make_current();
    void done_current();

private:
    void *gl_platform_create(void *plat_info);
    void gl_platform_destroy(void *plat);

    void *get_device_context_internal(void *param);

    void set_texture_share_enabled(bool enabled);

    void device_enter_context_internal(void *param);
    void device_leave_context_internal(void *param);

private:
    std::unique_ptr<gs_device_rc_private> d_ptr{};
};

class gs_device_context_helper
{
public:
    gs_device_context_helper(gs_context_gl *device_rc) : d(device_rc) {
        d->make_current();
    }
    ~gs_device_context_helper() {
        d->done_current();
    }

private:
    gs_context_gl *d{};
};
