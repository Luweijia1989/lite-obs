#include "lite-obs/graphics/gs_context_gl.h"
#include "lite-obs/graphics/gs_subsystem_info.h"
#include "lite-obs/util/log.h"

#if TARGET_PLATFORM == PLATFORM_WIN32

/* Basically swapchain-specific information.  Fortunately for windows this is
 * super basic stuff */
struct gl_windowinfo {
    HWND hwnd{};
    HDC hdc{};
};

/* Like the other subsystems, the GL subsystem has one swap chain created by
 * default. */
struct gl_platform {
    bool release_on_destroy = true;
    HGLRC hrc{};
    struct gl_windowinfo window{};

    ~gl_platform() {
        if (release_on_destroy) {
            if (hrc) {
                wglMakeCurrent(NULL, NULL);
                wglDeleteContext(hrc);
            }
        }
    }
};

gl_context_helper::gl_context_helper()
{
    platform = std::make_shared<gl_platform>();
    platform->release_on_destroy = false;
    platform->hrc = wglGetCurrentContext();
    platform->window.hdc = wglGetCurrentDC();
}

gl_context_helper::~gl_context_helper()
{
    if (platform->hrc && platform->window.hdc) {
        blog(LOG_INFO, "restore GL context.");
        wglMakeCurrent(platform->window.hdc, platform->hrc);
    }
}

#define DUMMY_WNDCLASS "Dummy GL Window Class"
static bool register_dummy_class(void)
{
    static bool created = false;

    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpfnWndProc = (WNDPROC)DefWindowProcA;
    wc.lpszClassName = DUMMY_WNDCLASS;

    if (created)
        return true;

    if (!RegisterClassA(&wc)) {
        blog(LOG_ERROR, "Failed to register dummy GL window class, %lu",
             GetLastError());
        return false;
    }

    created = true;
    return true;
}

void *gs_context_gl::gs_create_platform_rc()
{
    if (!register_dummy_class())
        return nullptr;

    auto plat = new gl_windowinfo();
    bool success = true;
    do {
        plat->hwnd = CreateWindowExA(0, DUMMY_WNDCLASS,
                                     "OpenGL Dummy Window", WS_POPUP, 0,
                                     0, 1, 1, NULL, NULL,
                                     GetModuleHandleW(NULL), NULL);
        if (!plat->hwnd) {
            blog(LOG_ERROR, "Failed to create dummy GL window, %lu", GetLastError());
            success = false;
            break;
        }

        plat->hdc = GetDC(plat->hwnd);
        if (!plat->hdc) {
            blog(LOG_ERROR, "Failed to get dummy GL window DC (%lu)", GetLastError());
            success = false;
            break;
        }
    }while(0);

    if (!success) {
        if (plat->hwnd) {
            if (plat->hdc)
                ReleaseDC(plat->hwnd, plat->hdc);

            DestroyWindow(plat->hwnd);
        }

        delete plat;
        return nullptr;
    }
    return plat;
}

void gs_context_gl::gs_destroy_platform_rc(void *plat)
{
    if (!plat)
        return;

    auto p = (gl_windowinfo *)plat;
    if (p->hwnd) {
        if (p->hdc)
            ReleaseDC(p->hwnd, p->hdc);

        DestroyWindow(p->hwnd);
    }
}

void gs_context_gl::device_enter_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    HDC hdc = plat->window.hdc;
    if (!wglMakeCurrent(hdc, plat->hrc)) {
        blog(LOG_ERROR, "device_enter_context (GL) failed");
    }
}

void gs_context_gl::device_leave_context_internal(void *param)
{
    if (!wglMakeCurrent(NULL, NULL))
        blog(LOG_DEBUG, "device_leave_context (GL) failed");
}

struct dummy_context {
    HWND hwnd{};
    HGLRC hrc{};
    HDC hdc{};

    ~dummy_context() {
        wglMakeCurrent(NULL, NULL);
        if (hrc)
            wglDeleteContext(hrc);

        if (hdc)
            ReleaseDC(hwnd, hdc);

        if (hwnd)
            DestroyWindow(hwnd);
    }
};

struct gs_init_data {
    void *hwnd{};
    uint32_t cx{}, cy{};
    uint32_t num_backbuffers{};
    gs_color_format format{};
    uint32_t adapter{};
};

static const char *dummy_window_class = "GLDummyWindow";
static bool registered_dummy_window_class = false;
/* Need a dummy window for the dummy context */
static bool gl_register_dummy_window_class(void)
{
    WNDCLASSA wc;
    if (registered_dummy_window_class)
        return true;

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpfnWndProc = DefWindowProc;
    wc.lpszClassName = dummy_window_class;

    if (!RegisterClassA(&wc)) {
        blog(LOG_ERROR, "Could not create dummy window class");
        return false;
    }

    registered_dummy_window_class = true;
    return true;
}

static inline HWND gl_create_dummy_window(void)
{
    HWND hwnd = CreateWindowExA(0, dummy_window_class, "Dummy GL Window",
                                WS_POPUP, 0, 0, 2, 2, NULL, NULL,
                                GetModuleHandle(NULL), NULL);
    if (!hwnd)
        blog(LOG_ERROR, "Could not create dummy context window");

    return hwnd;
}

/* would use designated initializers but Microsoft sort of sucks */
static inline void init_dummy_pixel_format(PIXELFORMATDESCRIPTOR *pfd)
{
    memset(pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    pfd->nSize = sizeof(pfd);
    pfd->nVersion = 1;
    pfd->iPixelType = PFD_TYPE_RGBA;
    pfd->cColorBits = 32;
    pfd->cDepthBits = 24;
    pfd->cStencilBits = 8;
    pfd->iLayerType = PFD_MAIN_PLANE;
    pfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                   PFD_DOUBLEBUFFER;
}

static inline HGLRC gl_init_basic_context(bool dummy, HDC hdc)
{
    HGLRC hglrc{};
    if (!dummy) {
        int attribList[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 2,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };

        hglrc = wglCreateContextAttribsARB(hdc, nullptr, attribList);
    } else
        hglrc = wglCreateContext(hdc);

    if (!hglrc) {
        blog(LOG_ERROR, "wglCreateContext failed, %lu", GetLastError());
        return NULL;
    }

    if (!wglMakeCurrent(hdc, hglrc)) {
        wglDeleteContext(hglrc);
        return NULL;
    }

    return hglrc;
}

static bool gl_dummy_context_init(dummy_context *dummy)
{
    if (!gl_register_dummy_window_class())
        return false;

    dummy->hwnd = gl_create_dummy_window();
    if (!dummy->hwnd)
        return false;

    dummy->hdc = GetDC(dummy->hwnd);

    PIXELFORMATDESCRIPTOR pfd;
    init_dummy_pixel_format(&pfd);
    auto format_index = ChoosePixelFormat(dummy->hdc, &pfd);
    if (!format_index) {
        blog(LOG_ERROR, "Dummy ChoosePixelFormat failed, %lu",
             GetLastError());
        return false;
    }

    if (!SetPixelFormat(dummy->hdc, format_index, &pfd)) {
        blog(LOG_ERROR, "Dummy SetPixelFormat failed, %lu",
             GetLastError());
        return false;
    }

    dummy->hrc = gl_init_basic_context(true, dummy->hdc);
    if (!dummy->hrc) {
        blog(LOG_ERROR, "Failed to initialize dummy context");
        return false;
    }

    return true;
}

static inline void required_extension_error(const char *extension)
{
    blog(LOG_ERROR, "OpenGL extension %s is required", extension);
}
static bool gl_init_extensions(HDC hdc)
{
    if (!gladLoadWGL(hdc)) {
        blog(LOG_ERROR, "Failed to load WGL entry functions.");
        return false;
    }

    if (!GLAD_WGL_ARB_pixel_format) {
        required_extension_error("ARB_pixel_format");
        return false;
    }

    if (!GLAD_WGL_ARB_create_context) {
        required_extension_error("ARB_create_context");
        return false;
    }

    if (!GLAD_WGL_ARB_create_context_profile) {
        required_extension_error("ARB_create_context_profile");
        return false;
    }

    return true;
}

/* For now, only support basic 32bit formats for graphics output. */
static inline int get_color_format_bits(gs_color_format format)
{
    switch (format) {
    case gs_color_format::GS_RGBA:
        return 32;
    default:
        return 0;
    }
}

/* Creates the real pixel format for the target window */
static int gl_choose_pixel_format(HDC hdc, const gs_init_data *info)
{
    int color_bits = get_color_format_bits(info->format);
    UINT num_formats;
    BOOL success;
    int format;

    if (!color_bits) {
        blog(LOG_ERROR, "gl_init_pixel_format: color format not "
                        "supported");
        return false;
    }

    int attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, color_bits,
        WGL_DEPTH_BITS_ARB, 0,
        WGL_STENCIL_BITS_ARB, 0,
        0, 0,
    };

    success = wglChoosePixelFormatARB(hdc, attribs, NULL, 1, &format,
                                      &num_formats);
    if (!success || !num_formats) {
        blog(LOG_ERROR, "wglChoosePixelFormatARB failed, %lu",
             GetLastError());
        format = 0;
    }

    return format;
}

static inline bool gl_getpixelformat(HDC hdc, const gs_init_data *info, int *format, PIXELFORMATDESCRIPTOR *pfd)
{
    if (!format)
        return false;

    *format = gl_choose_pixel_format(hdc, info);

    if (!DescribePixelFormat(hdc, *format, sizeof(*pfd), pfd)) {
        blog(LOG_ERROR, "DescribePixelFormat failed, %lu",
             GetLastError());
        return false;
    }

    return true;
}

static inline bool gl_setpixelformat(HDC hdc, int format, PIXELFORMATDESCRIPTOR *pfd)
{
    if (!SetPixelFormat(hdc, format, pfd)) {
        blog(LOG_ERROR, "SetPixelFormat failed, %lu", GetLastError());
        return false;
    }

    return true;
}
static bool init_default_swap(gl_platform *plat, int pixel_format, PIXELFORMATDESCRIPTOR *pfd)
{
    if (!gl_setpixelformat(plat->window.hdc, pixel_format, pfd))
        return false;

    return true;
}

void *gs_context_gl::gl_platform_create(void *plat_info)
{
    auto ctx = wglGetCurrentContext();
    auto dc = wglGetCurrentDC();
    if (ctx && dc)
        wglMakeCurrent(NULL, NULL);

    auto dummy = std::make_unique<dummy_context>();
    gs_init_data info;
    info.format = gs_color_format::GS_RGBA;
    int pixel_format;
    PIXELFORMATDESCRIPTOR pfd;

    if (!gl_dummy_context_init(dummy.get()))
        return nullptr;

    if (!gl_init_extensions(dummy->hdc))
        return nullptr;

    auto plat = std::make_unique<gl_platform>();
    auto p_info = (gl_windowinfo *)plat_info;
    plat->window.hwnd = p_info->hwnd;
    plat->window.hdc = p_info->hdc;
    if (!gl_getpixelformat(dummy->hdc, &info, &pixel_format, &pfd))
        return nullptr;

    dummy.reset();

    if (!init_default_swap(plat.get(), pixel_format, &pfd))
        return nullptr;

    plat->hrc = gl_init_basic_context(false, plat->window.hdc);
    if (!plat->hrc)
        return nullptr;

    if (ctx) {
        if (!wglShareLists(ctx, plat->hrc))
            blog(LOG_ERROR, "Failed to share OpenGL context.");
        else {
            set_texture_share_enabled(true);
            blog(LOG_INFO, "Share GL context with existing.");
        }
    }

    if (!gladLoadGL()) {
        blog(LOG_ERROR, "Failed to initialize OpenGL entry functions.");
        return nullptr;
    }

    return plat.release();
}

void gs_context_gl::gl_platform_destroy(void *plat)
{
    gl_platform *p = (gl_platform *)plat;
    delete p;
}

void *gs_context_gl::get_device_context_internal(void *param)
{
    gl_platform *plat = (gl_platform *)param;
    return plat->hrc;
}

#endif
