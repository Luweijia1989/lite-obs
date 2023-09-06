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

    std::shared_ptr<gs_simple_texture_drawer> texture_drawer{};

    EGLContext shared_ctx = EGL_NO_CONTEXT;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext egl_ctx = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;

    int egl_version = 0;
    EGLConfig egl_config = nullptr;

    AMediaCodec*    mediacodec = nullptr;
    ANativeWindow*  mediacodec_input_window = nullptr;
};

mediacodec_encoder::mediacodec_encoder(lite_obs_encoder *encoder)
    :lite_obs_encoder_interface(encoder)
{
    d_ptr = std::make_unique<mediacodec_encoder_private>();
    d_ptr->buffer = std::make_shared<std::vector<uint8_t>>();
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
    d_ptr->shared_ctx = ctx;
}

bool mediacodec_encoder::init_mediacodec()
{
    auto video = encoder->lite_obs_encoder_video();
    if (!video)
        return false;

    auto voi = video->video_output_get_info();
    d_ptr->fps_num = voi->fps_num;
    d_ptr->fps_den = voi->fps_den;
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
    AMediaFormat_setInt32(format, "color-format", 0x7F000789);
    AMediaFormat_setInt32(format, "bitrate", encoder->lite_obs_encoder_bitrate() * 1000);
    AMediaFormat_setInt32(format, "frame-rate", voi->fps_num / voi->fps_den);
    AMediaFormat_setInt32(format, "i-frame-interval", 2);
    //    AMediaFormat_setInt32(format, "profile", 0x1);
    AMediaFormat_setInt32(format, "max-bframes", 0);
#if __ANDROID_API__ >= 28
    AMediaFormat_setInt32(format, "bitrate-mode", 2); //CBR
#endif
    blog(LOG_INFO, "AMediaCodec_configure format : %s", AMediaFormat_toString(format));
    auto rc = AMediaCodec_configure(d_ptr->mediacodec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (rc != AMEDIA_OK) {
        blog(LOG_ERROR, "AMediaCodec_configure fail");
        return false;
    }
    AMediaFormat_delete(format);
    blog(LOG_INFO, "CodecEncoder AMediaCodec_configure %d ", rc);

    rc = AMediaCodec_createInputSurface(d_ptr->mediacodec, &d_ptr->mediacodec_input_window);
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

bool mediacodec_encoder::init_egl_related()
{
    d_ptr->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(d_ptr->display == EGL_NO_DISPLAY) {
        blog(LOG_ERROR, "eglGetDisplay wrong");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(d_ptr->display, &major, &minor) ) {
        d_ptr->display = NULL;
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
    if (!eglChooseConfig(d_ptr->display, attribute_list, &d_ptr->egl_config, 1, &numConfigs)) {
        return false;
    }

    const EGLint attrib3_list[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    d_ptr->egl_ctx = eglCreateContext(d_ptr->display, d_ptr->egl_config, d_ptr->shared_ctx, attrib3_list);
    if (eglGetError() != EGL_SUCCESS) {
        blog(LOG_ERROR, "error create encoder egl context.");
        return false;
    }

    int surfaceAttribs[] = {
        EGL_NONE
    };
    d_ptr->egl_surface = eglCreateWindowSurface(d_ptr->display, d_ptr->egl_config, d_ptr->mediacodec_input_window, surfaceAttribs);
    if (d_ptr->egl_surface == NULL) {
        blog(LOG_ERROR, "EGLSurface is NULL!");
        return false;
    }

    blog(LOG_INFO, "Mediacodec EGL success");
    return true;
}

bool mediacodec_encoder::i_create()
{
    if (!d_ptr->shared_ctx) {
        blog(LOG_ERROR, "mediacodec_encoder: OpenGL share context required");
        return false;
    }

    bool success = true;
    do {
        if (!init_mediacodec() || !init_egl_related()) {
            success = false;
            break;
        }
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

    if (d_ptr->display != EGL_NO_DISPLAY) {
        if (eglMakeCurrent(d_ptr->display, d_ptr->egl_surface, d_ptr->egl_surface, d_ptr->egl_ctx)) {
            d_ptr->texture_drawer.reset();
        }

        eglMakeCurrent(d_ptr->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d_ptr->egl_ctx)
            eglDestroyContext(d_ptr->display, d_ptr->egl_ctx);
        if (d_ptr->egl_surface)
            eglDestroySurface(d_ptr->display, d_ptr->egl_surface);
        eglReleaseThread();
        eglTerminate(d_ptr->display);
        d_ptr->display = EGL_NO_DISPLAY;
    }

    if (d_ptr->mediacodec_input_window) {
        ANativeWindow_release(d_ptr->mediacodec_input_window);
        d_ptr->mediacodec_input_window = nullptr;
    }

    blog(LOG_DEBUG, "mediacodec_encoder destroy");
}

bool mediacodec_encoder::i_encoder_valid()
{
    return d_ptr->initialized;
}

void mediacodec_encoder::draw_texture(int tex_id)
{
    glViewport(0, 0, d_ptr->width, d_ptr->height);
    if (!d_ptr->texture_drawer)
        d_ptr->texture_drawer = std::make_shared<gs_simple_texture_drawer>();

    d_ptr->texture_drawer->draw_texture(tex_id);
    glFlush();
}

#define AMEDIACODEC_BUFFER_FLAG_KEY_FRAME 1
#define TIMEOUT_USEC 8000
bool mediacodec_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void (std::shared_ptr<encoder_packet>)> send_off)
{
    if (!eglMakeCurrent(d_ptr->display, d_ptr->egl_surface, d_ptr->egl_surface, d_ptr->egl_ctx)) {
        blog(LOG_ERROR, "NOTE: eglMakeCurrent failed");
        return false;
    }

    int tex_id = *((int *)frame->data[0]);
    draw_texture(tex_id);

    auto pts = frame->pts * d_ptr->fps_den * 1000000 / d_ptr->fps_num;
    eglPresentationTimeANDROID(d_ptr->display, d_ptr->egl_surface, pts * 1000);

    if (!eglSwapBuffers(d_ptr->display, d_ptr->egl_surface)) {
        blog(LOG_ERROR, "NOTE: eglSwapBuffers failed");
        return false;
    }

    eglMakeCurrent(d_ptr->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );

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
    return true;
}

void mediacodec_encoder::i_update_encode_bitrate(int bitrate)
{

}

#endif
