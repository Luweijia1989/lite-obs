#include "lite-obs/graphics/gs_device.h"
#include "lite-obs/util/log.h"

#if TARGET_PLATFORM == PLATFORM_ANDROID

struct gl_platform
{
    EGLDisplay display{};
    EGLSurface surface{};
    EGLContext context{};
    bool release_on_destroy = true;

    ~gl_platform() {
        if (release_on_destroy) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglDestroySurface(display, surface);
            eglTerminate(display);
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
    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLDisplay display;
    EGLConfig config;
    EGLint numConfigs;
    EGLSurface surface;
    EGLContext context = nullptr;

    blog(LOG_DEBUG, "Initializing context");

    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        blog(LOG_DEBUG, "eglGetDisplay() returned error %d", eglGetError());
        return nullptr;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        blog(LOG_DEBUG, "eglInitialize() returned error %d", eglGetError());
        return nullptr;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        blog(LOG_DEBUG, "eglChooseConfig() returned error %d", eglGetError());

        return nullptr;
    }

    if (!(surface = eglCreatePbufferSurface(display, config, nullptr))) {
        blog(LOG_DEBUG, "eglCreateWindowSurface() returned error %d", eglGetError());
        return nullptr;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,  //Request opengl ES3.0
        EGL_NONE
    };
    auto ctx = eglGetCurrentContext();
    if (ctx) {
        EGLint contextVersion = 0;
        eglQueryContext(display, ctx, EGL_CONTEXT_CLIENT_VERSION, &contextVersion);
        if (contextVersion >= 3)
            if((context = eglCreateContext(display, config, ctx, contextAttribs)))
                set_texture_share_enabled(true);
    }

    if (!context) {
        if(!(context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs))){
            blog(LOG_DEBUG, "eglCreateContext() returned error %d", eglGetError());
            return nullptr;
        }

    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        blog(LOG_DEBUG, "eglMakeCurrent() returned error %d", eglGetError());
        return nullptr;
    }

    blog(LOG_DEBUG, "egl create opengles context success");

    auto plat = std::make_unique<gl_platform>();
    plat->context = context;
    plat->display = display;
    plat->surface = surface;

    return plat.release();
}

gl_context_helper::gl_context_helper()
{
    platform = std::make_shared<gl_platform>();
    platform->release_on_destroy = false;
    platform->context = eglGetCurrentContext();
    platform->display = eglGetCurrentDisplay();
    platform->surface = eglGetCurrentSurface(EGL_DRAW);
}

gl_context_helper::~gl_context_helper()
{
    if (platform->context && platform->display && platform->surface) {
        if (!eglMakeCurrent(platform->display, platform->surface, platform->surface, platform->context)) {
            blog(LOG_DEBUG, "gl_context_helper restore gl context-> %d", eglGetError());
        }
    }
}

void gs_device::device_enter_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    if (!eglMakeCurrent(plat->display, plat->surface, plat->surface, plat->context)) {
        blog(LOG_DEBUG, "eglMakeCurrent() returned error %d", eglGetError());
    }
}

void gs_device::device_leave_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    eglMakeCurrent(plat->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
}

void gs_device::gl_platform_destroy(void *plat)
{
    gl_platform *p = (gl_platform *)plat;
    delete p;
}

void *gs_device::get_device_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    return plat->context;
}

#endif
