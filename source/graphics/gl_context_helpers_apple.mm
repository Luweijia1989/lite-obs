#include "lite-obs/graphics/gl_context_helpers_apple.h"
#include "lite-obs/lite_obs_platform_config.h"

#ifdef PLATFORM_APPLE
#include "lite-obs/graphics/gs_subsystem_info.h"

#if TARGET_PLATFORM == PLATFORM_MAC
#include <AppKit/AppKit.h>
std::pair<void *, bool> gl_create_context(void *share_ctx)
{
    bool shared = false;
    NSOpenGLContext *ctx = nil;
    NSOpenGLContext *current_ctx = (NSOpenGLContext *)share_ctx;
    if (current_ctx) {
        GLint major, minor;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major >= 3 && minor >= 0) {
            ctx = [[NSOpenGLContext alloc] initWithFormat:[current_ctx pixelFormat] shareContext:current_ctx];
            if (ctx)
                shared = true;
        }
    }

    if (!ctx) {
        NSOpenGLPixelFormatAttribute pixelFormatAttributes[] = {
            NSOpenGLPFANoRecovery,    NSOpenGLPFAAccelerated,
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
            NSOpenGLPFAColorSize,     (NSOpenGLPixelFormatAttribute)32,
            NSOpenGLPFAAlphaSize,     (NSOpenGLPixelFormatAttribute)8,
            NSOpenGLPFADepthSize,     (NSOpenGLPixelFormatAttribute)24,
            NSOpenGLPFADoubleBuffer,  (NSOpenGLPixelFormatAttribute)0};

        NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttributes];
        ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
        [pf release];
    }

    return {ctx, shared};
}

void gl_destroy_context(void *ctx)
{
    NSOpenGLContext *c = (NSOpenGLContext *)ctx;
    [c release];
}

void *gl_current_context()
{
    return [NSOpenGLContext currentContext];
}

void gl_make_current(void *ctx)
{
    NSOpenGLContext *c = (NSOpenGLContext *)ctx;
    [c makeCurrentContext];
}

void gl_done_current()
{
    [NSOpenGLContext clearCurrentContext];
}

#else

#include <GLKit/GLKit.h>

std::pair<void *, bool> gl_create_context(void *share_ctx)
{
    bool shared = false;
    EAGLContext *ctx = nil;
    EAGLContext *current_ctx = (EAGLContext *)share_ctx;
    if (current_ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:[current_ctx API] sharegroup: [current_ctx sharegroup]];
        if (ctx)
            shared = true;
    }

    if (!ctx) {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!ctx)
            ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    }

    return {ctx, shared};
}


void gl_destroy_context(void *ctx)
{
    EAGLContext *c = (EAGLContext *)ctx;
    [c release];
}

void *gl_current_context()
{
    return [EAGLContext currentContext];
}

void gl_make_current(void *ctx)
{
    EAGLContext *c = (EAGLContext *)ctx;
    [EAGLContext setCurrentContext: c];
}

void gl_done_current()
{
    [EAGLContext setCurrentContext: nil];
}

#endif

#endif
