#include "lite-obs/graphics/gs_device.h"
#include "lite-obs/util/log.h"

#if TARGET_PLATFORM == PLATFORM_IOS

#import <GLKit/GLKit.h>

struct gl_platform
{
    EAGLContext *ctx{nullptr};
    bool release_on_destroy = true;
    ~gl_platform() {
        if (release_on_destroy) {
          [ctx release];
        }
    }
};

void *gs_device::gs_create_platform_rc()
{
    return nullptr;
}

void gs_device::gs_destroy_platform_rc(void *plat)
{
    (void)plat;
}

void *gs_device::gl_platform_create(void *)
{
    EAGLContext *ctx = nil;
    EAGLContext *current_ctx = [EAGLContext currentContext];
    if (current_ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:[current_ctx API] sharegroup: [current_ctx sharegroup]];
    }

    if (!ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!ctx)
          ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    }
    [EAGLContext setCurrentContext: ctx];

    gl_platform *plat = new gl_platform;
    plat->ctx = ctx;
    return plat;
}

gl_context_helper::gl_context_helper()
{
    platform = std::make_shared<gl_platform>();
    platform->release_on_destroy = false;
    platform->ctx = [EAGLContext currentContext];
}

gl_context_helper::~gl_context_helper()
{
    if (platform->ctx) {
        [EAGLContext setCurrentContext: platform->ctx];
    }
}

void gs_device::device_enter_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    [EAGLContext setCurrentContext: plat->ctx];
}

void gs_device::device_leave_context_internal(void *param)
{
    (void)param;
    [EAGLContext setCurrentContext: nil];
}

void gs_device::gl_platform_destroy(void *plat)
{
    gl_platform *p = (gl_platform *)plat;
    delete p;
}

#endif
