#include "lite-obs/encoder/mediacodec_encoder.h"

#if TARGET_PLATFORM == PLATFORM_ANDROID
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/graphics/gs_simple_texture_drawer.h"
#include "lite-obs/util/log.h"

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

struct surface_encode_rc
{
    std::shared_ptr<gs_simple_texture_drawer> texture_drawer{};

    EGLContext shared_ctx = EGL_NO_CONTEXT;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;

    int egl_version = 0;
    EGLConfig egl_config = nullptr;

    ANativeWindow*  mediacodec_input_window = nullptr;

    void reset() {
        if (display != EGL_NO_DISPLAY) {
            if (eglMakeCurrent(display, egl_surface, egl_surface, egl_ctx)) {
                texture_drawer.reset();
            }

            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (egl_ctx)
                eglDestroyContext(display, egl_ctx);
            if (egl_surface)
                eglDestroySurface(display, egl_surface);
            eglReleaseThread();
            eglTerminate(display);
            display = EGL_NO_DISPLAY;
        }

        if (mediacodec_input_window) {
            ANativeWindow_release(mediacodec_input_window);
            mediacodec_input_window = nullptr;
        }
    }

    bool init() {
        if (!shared_ctx) {
            blog(LOG_ERROR, "mediacodec_encoder: OpenGL share context required");
            return false;
        }

        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if(display == EGL_NO_DISPLAY) {
            blog(LOG_ERROR, "eglGetDisplay wrong");
            return false;
        }

        EGLint major = 0, minor = 0;
        if (!eglInitialize(display, &major, &minor) ) {
            display = NULL;
            blog(LOG_ERROR, "unable to initialize");
            return false;
        }

        int attribute_list[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_STENCIL_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RECORDABLE_ANDROID, 1,
            EGL_NONE
        };

        int numConfigs = 0;
        if (!eglChooseConfig(display, attribute_list, &egl_config, 1, &numConfigs)) {
            return false;
        }

        const EGLint attrib3_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        egl_ctx = eglCreateContext(display, egl_config, shared_ctx, attrib3_list);
        if (eglGetError() != EGL_SUCCESS) {
            blog(LOG_ERROR, "error create encoder egl context.");
            return false;
        }

        int surfaceAttribs[] = {
            EGL_NONE
        };
        egl_surface = eglCreateWindowSurface(display, egl_config, mediacodec_input_window, surfaceAttribs);
        if (egl_surface == NULL) {
            blog(LOG_ERROR, "EGLSurface is NULL!");
            return false;
        }

        blog(LOG_INFO, "Mediacodec EGL success");
        return true;
    }

    void draw_texture(int tex_id, int width, int height)
    {
        glViewport(0, 0, width, height);
        if (!texture_drawer)
            texture_drawer = std::make_shared<gs_simple_texture_drawer>();

        texture_drawer->draw_texture(tex_id);
        glFlush();
    }

    bool prepare_encode_texture(encoder_frame *frame, int width, int height, int fps_den, int fps_num) {
        if (!eglMakeCurrent(display, egl_surface, egl_surface, egl_ctx)) {
            blog(LOG_ERROR, "NOTE: eglMakeCurrent failed");
            return false;
        }

        int tex_id = *((int *)frame->data[0]);
        draw_texture(tex_id, width, height);

        auto pts = frame->pts * fps_den * 1000000 / fps_num;
        eglPresentationTimeANDROID(display, egl_surface, pts * 1000);

        if (!eglSwapBuffers(display, egl_surface)) {
            blog(LOG_ERROR, "NOTE: eglSwapBuffers failed");
            return false;
        }

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
        return true;
    }
};

struct mediacodec_encoder_private
{
    int width{};
    int height{};
    int fps_num{};
    int fps_den{};
    bool initialized = false;
    std::vector<uint8_t> sei;
    std::vector<uint8_t> header;
    std::shared_ptr<std::vector<uint8_t>> buffer;
    AMediaCodec*    mediacodec = nullptr;
    std::shared_ptr<surface_encode_rc> rc = nullptr;
    video_format format{};
};

mediacodec_encoder::mediacodec_encoder(lite_obs_encoder *encoder)
    :lite_obs_encoder_interface(encoder)
{
    d_ptr = std::make_unique<mediacodec_encoder_private>();
    d_ptr->buffer = std::make_shared<std::vector<uint8_t>>();
    d_ptr->rc = std::make_shared<surface_encode_rc>();
}

mediacodec_encoder::~mediacodec_encoder()
{
    if (mediacodec_encoder::i_encoder_valid())
        mediacodec_encoder::i_destroy();
}

const char *mediacodec_encoder::i_encoder_codec()
{
    return "h264-mediacodec";
}

obs_encoder_type mediacodec_encoder::i_encoder_type()
{
    return obs_encoder_type::OBS_ENCODER_VIDEO;
}

void mediacodec_encoder::i_set_gs_render_ctx(void *ctx)
{
    d_ptr->rc->shared_ctx = ctx;
}

bool mediacodec_encoder::format_valid()
{
    return d_ptr->format == video_format::VIDEO_FORMAT_NV12;
}

bool mediacodec_encoder::init_mediacodec()
{
    auto video = encoder->lite_obs_encoder_video();
    if (!video)
        return false;

    auto voi = video->video_output_get_info();
    d_ptr->fps_num = voi->fps_num;
    d_ptr->fps_den = voi->fps_den;
    d_ptr->format = voi->format;
    if (!format_valid())
        return false;

    d_ptr->mediacodec = AMediaCodec_createEncoderByType("video/avc");

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", "video/avc");
    d_ptr->width = encoder->lite_obs_encoder_get_width();
    d_ptr->height = encoder->lite_obs_encoder_get_height();
    if (d_ptr->width % 16 || d_ptr->height % 16) {
        blog(LOG_INFO, "Video size %dx%d isn't align to 16, it may have device compatibility issue", d_ptr->width, d_ptr->height);
    }
    AMediaFormat_setInt32(format, "width",  d_ptr->width);
    AMediaFormat_setInt32(format, "height", d_ptr->height);
    AMediaFormat_setInt32(format, "bitrate", encoder->lite_obs_encoder_bitrate() * 1000);
    AMediaFormat_setInt32(format, "frame-rate", voi->fps_num / voi->fps_den);
    AMediaFormat_setInt32(format, "i-frame-interval", 2);
    //    AMediaFormat_setInt32(format, "profile", 0x1);
    AMediaFormat_setInt32(format, "max-bframes", 0);
#if __ANDROID_API__ >= 28
    AMediaFormat_setInt32(format, "bitrate-mode", 2); //CBR
#endif

#if __ANDROID_API__ >= 26
    AMediaFormat_setInt32(format, "color-format", 0x7F000789);
#else
    AMediaFormat_setInt32(format, "color-format", 21);
#endif

    blog(LOG_INFO, "AMediaCodec_configure format : %s", AMediaFormat_toString(format));
    auto rc = AMediaCodec_configure(d_ptr->mediacodec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (rc != AMEDIA_OK) {
        blog(LOG_ERROR, "AMediaCodec_configure fail");
        return false;
    }
    AMediaFormat_delete(format);
    blog(LOG_INFO, "CodecEncoder AMediaCodec_configure %d ", rc);

#if __ANDROID_API__ >= 26
    rc = AMediaCodec_createInputSurface(d_ptr->mediacodec, &d_ptr->rc->mediacodec_input_window);
#endif

    if(AMEDIA_OK == rc) {
        rc = AMediaCodec_start(d_ptr->mediacodec);
        blog(LOG_INFO, "CodecEncoder AMediaCodec_start %d ",rc);

        blog(LOG_INFO, "settings:\n"
                       "\trate_control:     CBR\n"
                       "\tbitrate:          %d\n"
                       "\tfps_num:          %d\n"
                       "\tfps_den:          %d\n"
                       "\twidth:            %d\n"
                       "\theight:           %d\n"
                       "\ti-frame-interval: 2\n",
             encoder->lite_obs_encoder_bitrate(),
             voi->fps_num, voi->fps_den,
             d_ptr->width, d_ptr->height);

        return (rc==AMEDIA_OK);
    } else {
        blog(LOG_WARNING, "CodecEncoder AMediaCodec_createInputSurface != OK.");
        AMediaCodec_delete(d_ptr->mediacodec);
        d_ptr->mediacodec = nullptr;
        return false;
    }
}

bool mediacodec_encoder::i_create()
{
    bool success = true;
    do {
        if (!init_mediacodec()) {
            success = false;
            break;
        }
#if __ANDROID_API__ >= 26
        if (!d_ptr->rc->init()) {
            success = false;
            break;
        }
#endif

    } while(0);

    if (!success)
        i_destroy();

    d_ptr->initialized = success;
    return success;
}

void mediacodec_encoder::i_destroy()
{
    if (d_ptr->mediacodec) {
        AMediaCodec_flush(d_ptr->mediacodec);
        AMediaCodec_stop(d_ptr->mediacodec);
        AMediaCodec_delete(d_ptr->mediacodec);
        d_ptr->mediacodec = NULL;
    }

    d_ptr->rc->reset();

    blog(LOG_DEBUG, "mediacodec_encoder destroy");
}

bool mediacodec_encoder::i_encoder_valid()
{
    return d_ptr->initialized;
}

#define AMEDIACODEC_BUFFER_FLAG_KEY_FRAME 1
#define TIMEOUT_USEC 8000
bool mediacodec_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void (std::shared_ptr<encoder_packet>)> send_off)
{
#if __ANDROID_API__ >= 26
    if (!d_ptr->rc->prepare_encode_texture(frame, d_ptr->width, d_ptr->height, d_ptr->fps_den, d_ptr->fps_num))
        return false;
#else
    while (true) {
        // nv12
        ssize_t idx = AMediaCodec_dequeueInputBuffer(d_ptr->mediacodec, TIMEOUT_USEC);
        if(idx>=0) {
            size_t buf_size = 0;
            auto pts = frame->pts * d_ptr->fps_den * 1000000 / d_ptr->fps_num;
            uint8_t *buf = AMediaCodec_getInputBuffer(d_ptr->mediacodec, idx, &buf_size);
            int copy_index = 0;
            memcpy(buf + copy_index, frame->data[0], frame->linesize[0] * d_ptr->height);
            copy_index += frame->linesize[0] * d_ptr->height;
            memcpy(buf + copy_index, frame->data[1], frame->linesize[1] * d_ptr->height / 2);
            AMediaCodec_queueInputBuffer(d_ptr->mediacodec, idx, 0, d_ptr->width * d_ptr->height * 3 / 2, pts, 0);
            break;
        }
    }
#endif

    try {
        while (true) {
            AMediaCodecBufferInfo info;
            auto status = AMediaCodec_dequeueOutputBuffer(d_ptr->mediacodec, &info, TIMEOUT_USEC);
            if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                break;
            } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                blog(LOG_DEBUG, "encoder output buffers changed");
            } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                auto format = AMediaCodec_getOutputFormat(d_ptr->mediacodec);
                blog(LOG_INFO, "format changed to: %s", AMediaFormat_toString(format));
                AMediaFormat_delete(format);
            } else if (status < 0) {
                blog(LOG_DEBUG, "unexpected result from encoder.dequeueOutputBuffer: %d", status);
                break;
            } else {
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                    blog(LOG_INFO, "output EOS");
                    break;
                }

                size_t buffer_size = 0;
                uint8_t* output_buf = AMediaCodec_getOutputBuffer(d_ptr->mediacodec, status, &buffer_size);
                if (!output_buf) {
                    blog(LOG_WARNING, "FBI WARMING: output_buf nullptr!");
                    break;
                }

                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    blog(LOG_INFO, "capture Video BUFFER_FLAG_CODEC_CONFIG.");
                    d_ptr->header.resize(info.size);
                    memcpy(d_ptr->header.data(), output_buf, info.size);
                } else {
                    int bytes = info.size;
                    uint8_t sei_buf[102400] = {0};
                    int sei_len = 0;
                    bool got_sei = encoder->lite_obs_encoder_get_sei(sei_buf, &sei_len);
                    if (got_sei) {
                        bytes += sei_len;
                    }

                    d_ptr->buffer->clear();
                    d_ptr->buffer->resize(bytes);

                    int copy_index = 0;
                    if (got_sei) {
                        memcpy(d_ptr->buffer->data(), sei_buf, sei_len);
                        copy_index += sei_len;
                    }

                    memcpy(d_ptr->buffer->data() + copy_index, output_buf, info.size);
                    packet->data = d_ptr->buffer;
                    packet->type = obs_encoder_type::OBS_ENCODER_VIDEO;

                    packet->pts = info.presentationTimeUs / (d_ptr->fps_den * 1000000 / d_ptr->fps_num);
                    packet->dts = packet->pts;
                    packet->keyframe = !(info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME);
                    send_off(packet);
                }
                AMediaCodec_releaseOutputBuffer(d_ptr->mediacodec, status, false);
            }
        }
    } catch (char *str) {
        blog(LOG_INFO, "AMediaCodec_encode_thread_err : %s", str);
    }

    return true;
}

bool mediacodec_encoder::i_get_extra_data(uint8_t **extra_data, size_t *size)
{
    *extra_data = d_ptr->header.data();
    *size = d_ptr->header.size();
    return true;
}

bool mediacodec_encoder::i_get_sei_data(uint8_t **sei_data, size_t *size)
{
    *sei_data = d_ptr->sei.data();
    *size = d_ptr->sei.size();
    return true;
}

bool mediacodec_encoder::i_gpu_encode_available()
{
#if __ANDROID_API__ >= 26
    return true;
#else
    return false;
#endif
}

void mediacodec_encoder::i_update_encode_bitrate(int bitrate)
{

}

#endif
