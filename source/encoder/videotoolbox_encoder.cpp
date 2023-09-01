#include "lite-obs/encoder/videotoolbox_encoder.h"

#ifdef PLATFORM_APPLE
#include "lite-obs/media-io/video_output.h"
#include "lite-obs/util/log.h"
#include "lite-obs/lite_obs_avc.h"
#include "lite-obs/graphics/gl_context_helpers_apple.h"
#include "lite-obs/graphics/gs_simple_texture_drawer.h"

#include <CoreFoundation/CoreFoundation.h>
#include <VideoToolbox/VideoToolbox.h>
#include <VideoToolbox/VTVideoEncoderList.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <string>


static CFStringRef lite_obs_to_vt_colorspace(video_colorspace cs)
{
    switch (cs) {
    case video_colorspace::VIDEO_CS_601:
        return kCVImageBufferYCbCrMatrix_ITU_R_601_4;
    default:
        return kCVImageBufferYCbCrMatrix_ITU_R_709_2;
    }
}

static CFStringRef lite_obs_to_vt_primaries(video_colorspace cs)
{
    switch (cs) {
    case video_colorspace::VIDEO_CS_601:
        return kCVImageBufferColorPrimaries_SMPTE_C;
    default:
        return kCVImageBufferColorPrimaries_ITU_R_709_2;
    }
}

static CFStringRef lite_obs_to_vt_transfer(video_colorspace cs)
{
    return kCVImageBufferTransferFunction_ITU_R_709_2;
}

struct videotoolbox_encoder_private
{
    bool initialized{};
    void *shared_ctx{};

#if TARGET_PLATFORM == PLATFORM_IOS
    bool texture_encode_initialized{};
    void *gl_ctx{};
    CVOpenGLESTextureCacheRef texture_cache{};
    CVOpenGLESTextureRef texture{};
    CVPixelBufferRef target{};
    GLuint fbo{};
    GLuint depth_buffer{};
    std::unique_ptr<gs_simple_texture_drawer> texture_drawer{};
#endif

    std::string vt_encoder_id;
    uint32_t width{};
    uint32_t height{};
    uint32_t keyint{};
    uint32_t fps_num{};
    uint32_t fps_den{};
    std::string rate_control;
    uint32_t bitrate{};
    float quality{};
    bool limit_bitrate{};
    uint32_t rc_max_bitrate{};
    double rc_max_bitrate_window{};
    std::string profile;
    CMVideoCodecType codec_type{};
    bool bframes{};

    int vt_pix_fmt{};
    video_colorspace colorspace{};

    VTCompressionSessionRef session{};
    CMSimpleQueueRef queue{};
    bool hw_enc{};

    std::shared_ptr<std::vector<uint8_t>> packet_data;
    std::vector<uint8_t> extra_data;
    std::vector<uint8_t> sei;

    videotoolbox_encoder_private() {
        packet_data = std::make_shared<std::vector<uint8_t>>();
    }
};

videotoolbox_encoder::videotoolbox_encoder(lite_obs_encoder *encoder)
    :lite_obs_encoder_interface(encoder)
{
    d_ptr = std::make_unique<videotoolbox_encoder_private>();
}

videotoolbox_encoder::~videotoolbox_encoder()
{
    if (videotoolbox_encoder::i_encoder_valid())
        videotoolbox_encoder::i_destroy();
}

const char *videotoolbox_encoder::i_encoder_codec()
{
    return "h264-videotoolbox";
}

obs_encoder_type videotoolbox_encoder::i_encoder_type()
{
    return obs_encoder_type::OBS_ENCODER_VIDEO;
}

void videotoolbox_encoder::i_set_gs_render_ctx(void *ctx)
{
    d_ptr->shared_ctx = ctx;
}

bool videotoolbox_encoder::update_params()
{
    auto video = encoder->lite_obs_encoder_video();
    if (!video)
        return false;

    auto voi = video->video_output_get_info();
    d_ptr->codec_type = kCMVideoCodecType_H264;
    d_ptr->vt_pix_fmt = voi->range == video_range_type::VIDEO_RANGE_FULL ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    d_ptr->colorspace = voi->colorspace;
    d_ptr->width = encoder->lite_obs_encoder_get_width();
    d_ptr->height = encoder->lite_obs_encoder_get_height();
    d_ptr->fps_num = voi->fps_num;
    d_ptr->fps_den = voi->fps_den;
    d_ptr->keyint = 2;
    d_ptr->rate_control = "ABR";
    d_ptr->bitrate = encoder->lite_obs_encoder_bitrate();;
    d_ptr->quality = 60.f / 100.f;
    d_ptr->profile = "high";
    d_ptr->limit_bitrate = false;
    d_ptr->rc_max_bitrate = d_ptr->bitrate;
    d_ptr->rc_max_bitrate_window = 1.5;
    d_ptr->bframes = false;

    return true;
}

static inline CFDictionaryRef create_encoder_spec(const char *vt_encoder_id)
{
    CFTypeRef keys[1] = {kVTVideoEncoderSpecification_EncoderID};
    CFStringRef id = CFStringCreateWithFileSystemRepresentation(nullptr, vt_encoder_id);
    CFTypeRef values[1] = {id};

    CFDictionaryRef encoder_spec =
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                           &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);

    CFRelease(id);

    return encoder_spec;
}

#if TARGET_PLATFORM == PLATFORM_MAC
static inline CFDictionaryRef create_pixbuf_spec(int vt_pix_fmt, int width, int height)
{
    CFNumberRef PixelFormat = CFNumberCreate(
        kCFAllocatorDefault, kCFNumberSInt32Type, &vt_pix_fmt);
    CFNumberRef Width = CFNumberCreate(kCFAllocatorDefault,
                                       kCFNumberSInt32Type, &width);
    CFNumberRef Height = CFNumberCreate(kCFAllocatorDefault,
                                        kCFNumberSInt32Type, &height);

    CFTypeRef keys[3] = {kCVPixelBufferPixelFormatTypeKey,
                         kCVPixelBufferWidthKey, kCVPixelBufferHeightKey};
    CFTypeRef values[3] = {PixelFormat, Width, Height};

    CFDictionaryRef pixbuf_spec =
        CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
                           &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);

    CFRelease(PixelFormat);
    CFRelease(Width);
    CFRelease(Height);

    return pixbuf_spec;
}
#endif

void sample_encoded_callback(void *data, void *source, OSStatus status,
                             VTEncodeInfoFlags info_flags,
                             CMSampleBufferRef buffer)
{

    if (status != noErr) {
        blog(LOG_ERROR, "videotoolbox: encoder callback: %d", status);
        return;
    }

    if (info_flags == kVTEncodeInfo_FrameDropped) {
        blog(LOG_INFO, "videotoolbox: Frame dropped by encoder");
    }

    CMSimpleQueueRef queue = (CMSimpleQueueRef)data;
    if (buffer != nullptr) {
        CFRetain(buffer);
        CMSimpleQueueEnqueue(queue, buffer);
    }
#if TARGET_PLATFORM == PLATFORM_MAC
    CVPixelBufferRef pixbuf = (CVPixelBufferRef)source;
    CFRelease(pixbuf);
#endif
}

static OSStatus session_set_prop_int(VTCompressionSessionRef session, CFStringRef key, int32_t val)
{
    CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &val);
    OSStatus code = VTSessionSetProperty(session, key, n);
    CFRelease(n);

    return code;
}

static OSStatus session_set_prop(VTCompressionSessionRef session, CFStringRef key, CFTypeRef val)
{
    return VTSessionSetProperty(session, key, val);
}

static OSStatus session_set_colorspace(VTCompressionSessionRef session, video_colorspace cs)
{
    CFTypeRef keys[3] = {kVTCompressionPropertyKey_ColorPrimaries,
                         kVTCompressionPropertyKey_TransferFunction,
                         kVTCompressionPropertyKey_YCbCrMatrix};

    CFTypeRef values[3] = {lite_obs_to_vt_primaries(cs), lite_obs_to_vt_transfer(cs),
                           lite_obs_to_vt_colorspace(cs)};

    CFDataRef masteringDisplayColorVolume = nullptr;
    CFDataRef contentLightLevel = nullptr;

    CFDictionaryRef session_properties = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
                                                            &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);

    auto code = VTSessionSetProperties(session, session_properties);

    CFRelease(session_properties);

    if (masteringDisplayColorVolume != nullptr) {
        CFRelease(masteringDisplayColorVolume);
    }

    if (contentLightLevel != nullptr) {
        CFRelease(contentLightLevel);
    }

    return code;
}

bool videotoolbox_encoder::create_encoder()
{
    CFDictionaryRef encoder_spec = create_encoder_spec(d_ptr->vt_encoder_id.c_str());
    VTCompressionSessionRef s;
#if TARGET_PLATFORM == PLATFORM_MAC
    CFDictionaryRef pixbuf_spec = create_pixbuf_spec(d_ptr->vt_pix_fmt, d_ptr->width, d_ptr->height);
    auto code = VTCompressionSessionCreate(kCFAllocatorDefault, d_ptr->width,
                                           d_ptr->height, d_ptr->codec_type,
                                           encoder_spec, pixbuf_spec, nullptr,
                                           &sample_encoded_callback, d_ptr->queue,
                                           &s);

    CFRelease(pixbuf_spec);
#else
    // iOS does not need a pixel buffer pool
    auto code = VTCompressionSessionCreate(kCFAllocatorDefault, d_ptr->width,
                                           d_ptr->height, d_ptr->codec_type,
                                           encoder_spec, nullptr, nullptr,
                                           &sample_encoded_callback, d_ptr->queue,
                                           &s);
#endif

    if (code != noErr) {
        blog(LOG_ERROR, "videotoolbox: VTCompressionSessionCreate: %d", code);
    }

    CFRelease(encoder_spec);

#if TARGET_PLATFORM == PLATFORM_MAC
    CFBooleanRef b = nullptr;
    code = VTSessionCopyProperty(s, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, nullptr, &b);

    if (code == noErr && (d_ptr->hw_enc = CFBooleanGetValue(b)))
        blog(LOG_INFO, "videotoolbox: session created with hardware encoding");
    else
        d_ptr->hw_enc = false;

    if (b != nullptr)
        CFRelease(b);
#else
    d_ptr->hw_enc = true;
#endif

    // This can fail when using GPU hardware encoding
    code = session_set_prop_int(s, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, d_ptr->keyint);
    if (code != noErr)
        blog(LOG_WARNING,
             "videotoolbox: setting kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration failed, "
             "keyframe interval might be incorrect: %d", code);

    CFTypeRef session_keys[4] = {
                                 kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                 kVTCompressionPropertyKey_ExpectedFrameRate,
                                 kVTCompressionPropertyKey_AllowFrameReordering,
                                 kVTCompressionPropertyKey_ProfileLevel};

    SInt32 key_frame_interval = (SInt32)(d_ptr->keyint * ((float)d_ptr->fps_num / d_ptr->fps_den));
    float expected_framerate = (float)d_ptr->fps_num / d_ptr->fps_den;
    CFNumberRef MaxKeyFrameInterval = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &key_frame_interval);
    CFNumberRef ExpectedFrameRate = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat32Type, &expected_framerate);
    CFTypeRef AllowFrameReordering = d_ptr->bframes ? kCFBooleanTrue : kCFBooleanFalse;

    CFTypeRef ProfileLevel = kVTProfileLevel_H264_High_AutoLevel;

    CFTypeRef session_values[4] = {MaxKeyFrameInterval,
                                   ExpectedFrameRate,
                                   AllowFrameReordering,
                                   ProfileLevel};

    CFDictionaryRef session_properties = CFDictionaryCreate(
        kCFAllocatorDefault, session_keys, session_values, 4,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);

    code = VTSessionSetProperties(s, session_properties);

    CFRelease(MaxKeyFrameInterval);
    CFRelease(ExpectedFrameRate);
    CFRelease(AllowFrameReordering);
    CFRelease(ProfileLevel);
    CFRelease(session_properties);

    if (code != noErr) {
        return code;
    }

    code = session_set_bitrate(s);
    if (code != noErr) {
        return code;
    }


    // This can fail depending on hardware configuration
    code = session_set_prop(s, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    if (code != noErr)
        blog(LOG_WARNING,
             "videotoolbox: setting kVTCompressionPropertyKey_RealTime failed, "
             "frame delay might be increased: %d", code);

    code = session_set_colorspace(s, d_ptr->colorspace);
    if (code != noErr) {
        return code;
    }

    code = VTCompressionSessionPrepareToEncodeFrames(s);
    if (code != noErr) {
        return code;
    }

    d_ptr->session = s;

    return true;
}

void videotoolbox_encoder::dump_encoder_info()
{
    blog(LOG_INFO,
         "videotoolbox: settings->\n"
         "\tvt_encoder_id          %s\n"
         "\trate_control:          %s\n"
         "\tbitrate:               %d (kbps)\n"
         "\tquality:               %f\n"
         "\tfps_num:               %d\n"
         "\tfps_den:               %d\n"
         "\twidth:                 %d\n"
         "\theight:                %d\n"
         "\tkeyint:                %d (s)\n"
         "\tlimit_bitrate:         %s\n"
         "\trc_max_bitrate:        %d (kbps)\n"
         "\trc_max_bitrate_window: %f (s)\n"
         "\thw_enc:                %s\n"
         "\tprofile:               %s\n"
         "\tcodec_type:            %.4s\n",
         d_ptr->vt_encoder_id.c_str(), d_ptr->rate_control.c_str(), d_ptr->bitrate,
         d_ptr->quality, d_ptr->fps_num, d_ptr->fps_den, d_ptr->width,
         d_ptr->height, d_ptr->keyint, d_ptr->limit_bitrate ? "on" : "off",
         d_ptr->rc_max_bitrate, d_ptr->rc_max_bitrate_window,
         d_ptr->hw_enc ? "on" : "off",
         d_ptr->profile.empty() ? "default" : d_ptr->profile.c_str(),
         "h264");
}

int videotoolbox_encoder::session_set_bitrate(void *session)
{
    CFStringRef compressionPropertyKey = kVTCompressionPropertyKey_AverageBitRate;
    bool can_limit_bitrate = true;


    if (compressionPropertyKey != kVTCompressionPropertyKey_Quality) {
        auto code = session_set_prop_int((VTCompressionSessionRef)session, compressionPropertyKey, d_ptr->bitrate * 1000);
        if (code != noErr) {
            return code;
        }
    }

    if (d_ptr->limit_bitrate && can_limit_bitrate) {
        double cpb_size = d_ptr->rc_max_bitrate * 125 * d_ptr->rc_max_bitrate_window;

        CFNumberRef cf_cpb_size = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &cpb_size);
        CFNumberRef cf_cpb_window_size = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &d_ptr->rc_max_bitrate_window);

        CFTypeRef values[2] = {cf_cpb_size, cf_cpb_window_size};

        CFArrayRef rate_control_data = CFArrayCreate(kCFAllocatorDefault, values, 2, &kCFTypeArrayCallBacks);

        auto code = session_set_prop((VTCompressionSessionRef)session, kVTCompressionPropertyKey_DataRateLimits, rate_control_data);

        CFRelease(cf_cpb_size);
        CFRelease(cf_cpb_window_size);
        CFRelease(rate_control_data);

        if (code == kVTPropertyNotSupportedErr) {
            blog(LOG_WARNING,"videotoolbox: setting DataRateLimits on session: %d", code);
            return noErr;
        }
    }

    return noErr;
}

bool videotoolbox_encoder::i_create()
{
#if TARGET_PLATFORM == PLATFORM_IOS
    if (!d_ptr->shared_ctx)
        return false;

    auto pair = gl_create_context(d_ptr->shared_ctx);
    if (!pair.second) {
        gl_destroy_context(pair.first);
        return false;
    }

    d_ptr->gl_ctx = pair.first;
#endif

    CFArrayRef encoderList;
    OSStatus status = VTCopyVideoEncoderList(nullptr, &encoderList);

    if (status == noErr) {
        CFIndex count = CFArrayGetCount(encoderList);

        for (CFIndex i = 0; i < count; i++) {
            CFDictionaryRef encoder_dict = (CFDictionaryRef)CFArrayGetValueAtIndex(encoderList, i);
            CMVideoCodecType codec_type = 0;
            {
                CFNumberRef codec_type_num = (CFNumberRef)CFDictionaryGetValue(encoder_dict, kVTVideoEncoderList_CodecType);
                CFNumberGetValue(codec_type_num, kCFNumberSInt32Type, &codec_type);
            }

            if (codec_type == kCMVideoCodecType_H264) {
                CFStringRef EncoderID = (CFStringRef)CFDictionaryGetValue(encoder_dict, kVTVideoEncoderList_EncoderID);
                CFIndex id_len = CFStringGetMaximumSizeOfFileSystemRepresentation(EncoderID);
                d_ptr->vt_encoder_id.resize(id_len);
                CFStringGetFileSystemRepresentation(EncoderID, d_ptr->vt_encoder_id.data(), id_len);

                CFBooleanRef hardware_ref = (CFBooleanRef)CFDictionaryGetValue(encoder_dict, kVTVideoEncoderList_IsHardwareAccelerated);
                d_ptr->hw_enc = (hardware_ref) ? CFBooleanGetValue(hardware_ref) : false;
                break;
            }
        }

        CFRelease(encoderList);
    } else
        blog(LOG_INFO, "videotoolbox: fail to get encoder list-->%d.", status);

    if (d_ptr->vt_encoder_id.empty()) {
        blog(LOG_INFO, "videotoolbox: no h264 encoder found.");
        return false;
    }

    bool success = true;
    do {
        if (!update_params()) {
            success = false;
            break;
        }

        auto code = CMSimpleQueueCreate(nullptr, 100, &d_ptr->queue);
        if (code != noErr) {
            success = false;
            break;
        }

        if (!create_encoder()) {
            success = false;
            break;
        }
    } while (0);

    dump_encoder_info();

    if (!success)
        i_destroy();
    else
        d_ptr->initialized = true;

    return success;
}

void videotoolbox_encoder::i_destroy()
{
    if (d_ptr->session != nullptr) {
        VTCompressionSessionInvalidate(d_ptr->session);
        CFRelease(d_ptr->session);
    }

#if TARGET_PLATFORM == PLATFORM_IOS
    if (d_ptr->gl_ctx) {
        gl_make_current(d_ptr->gl_ctx);

        if (d_ptr->target) {
            CFRelease(d_ptr->target);
        }

        if (d_ptr->texture_cache) {
            CFRelease(d_ptr->texture_cache);
        }

        if (d_ptr->texture) {
            CFRelease(d_ptr->texture);
        }

        if (d_ptr->fbo > 0) {
            glDeleteFramebuffers(1, &d_ptr->fbo);
            d_ptr->fbo = 0;
        }

        if (d_ptr->depth_buffer > 0) {
            glDeleteRenderbuffers(1, &d_ptr->depth_buffer);
            d_ptr->depth_buffer = 0;
        }

        d_ptr->texture_drawer.reset();

        gl_done_current();

        gl_destroy_context(d_ptr->gl_ctx);
        d_ptr->gl_ctx = nullptr;
        d_ptr->texture_encode_initialized = false;
    }
#endif

    d_ptr->initialized = false;
}

bool videotoolbox_encoder::i_encoder_valid()
{
    return d_ptr->initialized;
}

#if TARGET_PLATFORM == PLATFORM_MAC
static bool get_cached_pixel_buffer(VTCompressionSessionRef session, video_colorspace cs, CVPixelBufferRef *buf)
{
    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(session);
    if (!pool)
        return kCVReturnError;

    CVPixelBufferRef pixbuf;
    auto code = CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &pixbuf);
    if (code != noErr) {
        goto fail;
    }

    CVBufferSetAttachment(pixbuf, kCVImageBufferYCbCrMatrixKey,
                          lite_obs_to_vt_colorspace(cs),
                          kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixbuf, kCVImageBufferColorPrimariesKey,
                          lite_obs_to_vt_primaries(cs),
                          kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixbuf, kCVImageBufferTransferFunctionKey,
                          lite_obs_to_vt_transfer(cs),
                          kCVAttachmentMode_ShouldPropagate);

    *buf = pixbuf;
    return true;

fail:
    return false;
}
#endif

static bool is_sample_keyframe(CMSampleBufferRef buffer)
{
    CFArrayRef attachments =
        CMSampleBufferGetSampleAttachmentsArray(buffer, false);
    if (attachments != nullptr) {
        CFDictionaryRef attachment;
        CFBooleanRef has_dependencies;
        attachment =
            (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        has_dependencies = (CFBooleanRef)CFDictionaryGetValue(
            attachment, kCMSampleAttachmentKey_DependsOnOthers);
        return has_dependencies == kCFBooleanFalse;
    }

    return false;
}

static const uint8_t annexb_startcode[4] = {0, 0, 0, 1};

static bool handle_keyframe(CMFormatDescriptionRef format_desc,
                            size_t param_count, const std::shared_ptr<std::vector<uint8_t>> &packet,
                            std::vector<uint8_t> &extra_data)
{
    for (size_t i = 0; i < param_count; i++) {
        const uint8_t *param = nullptr;
        size_t param_size = 0;
        auto code = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format_desc, i, &param, &param_size, nullptr, nullptr);

        if (code != noErr) {
            blog(LOG_ERROR, "videotoolbox: getting NAL parameter at index: %d", code);
            return false;
        }

        packet->insert(packet->end(), annexb_startcode, annexb_startcode + 4);
        packet->insert(packet->end(), param, param + param_size);
    }

    // if we were passed an extra_data array, fill it with
    // SPS, PPS, etc.
    if (extra_data.empty()) {
        extra_data.resize(packet->size());
        memcpy(extra_data.data(), packet->data(), packet->size());
    }

    return true;
}

static void convert_block_nals_to_annexb(const std::shared_ptr<std::vector<uint8_t>> &packet,
                                         CMBlockBufferRef block,
                                         int nal_length_bytes)
{
    size_t block_size = 0;
    uint8_t *block_buf = nullptr;
    CMBlockBufferGetDataPointer(block, 0, nullptr, &block_size, (char **)&block_buf);

    size_t bytes_remaining = block_size;
    while (bytes_remaining > 0) {
        uint32_t nal_size;
        if (nal_length_bytes == 1)
            nal_size = block_buf[0];
        else if (nal_length_bytes == 2)
            nal_size = CFSwapInt16BigToHost(
                ((uint16_t *)block_buf)[0]);
        else if (nal_length_bytes == 4)
            nal_size = CFSwapInt32BigToHost(
                ((uint32_t *)block_buf)[0]);
        else
            return;

        bytes_remaining -= nal_length_bytes;
        block_buf += nal_length_bytes;

        if (bytes_remaining < nal_size) {
            blog(LOG_ERROR, "videotoolbox: invalid nal block");
            return;
        }

        packet->insert(packet->end(), annexb_startcode + 1, annexb_startcode + 4);
        packet->insert(packet->end(), block_buf, block_buf + nal_size);

        bytes_remaining -= nal_size;
        block_buf += nal_size;
    }
}

bool videotoolbox_encoder::convert_sample_to_annexb(void *buf, bool keyframe)
{
    CMSampleBufferRef buffer = (CMSampleBufferRef)buf;
    CMFormatDescriptionRef format_desc = CMSampleBufferGetFormatDescription(buffer);

    size_t param_count = 0;
    int nal_length_bytes = 0;
    auto code = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
        format_desc, 0, nullptr, nullptr, &param_count,
        &nal_length_bytes);

    if (code == kCMFormatDescriptionBridgeError_InvalidParameter ||
        code == kCMFormatDescriptionError_InvalidParameter) {
        blog(LOG_WARNING, "videotoolbox: assuming 2 parameter sets and 4 byte NAL length header");
        param_count = 2;
        nal_length_bytes = 4;

    } else if (code != noErr) {
        blog(LOG_ERROR, "videotoolbox: getting parameter count from sample %d", code);
        return false;
    }

    if (keyframe && !handle_keyframe(format_desc, param_count, d_ptr->packet_data, d_ptr->extra_data))
        return false;

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(buffer);
    convert_block_nals_to_annexb(d_ptr->packet_data, block, nal_length_bytes);

    return true;
}

bool videotoolbox_encoder::parse_sample(void *buf, const std::shared_ptr<encoder_packet> &packet, CMTime off, std::function<void(std::shared_ptr<encoder_packet>)> send_off)
{
    CMSampleBufferRef buffer = (CMSampleBufferRef)buf;
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(buffer);
    CMTime dts = CMSampleBufferGetDecodeTimeStamp(buffer);

    if (CMTIME_IS_INVALID(dts))
        dts = pts;
    // imitate x264's negative dts when bframes might have pts < dts
    else if (d_ptr->bframes)
        dts = CMTimeSubtract(dts, off);

    pts = CMTimeMultiply(pts, d_ptr->fps_num);
    dts = CMTimeMultiply(dts, d_ptr->fps_num);

    const bool keyframe = is_sample_keyframe(buffer);
    if (!convert_sample_to_annexb(buffer, keyframe)) {
        CFRelease(buffer);
        return false;
    }

    packet->type = obs_encoder_type::OBS_ENCODER_VIDEO;
    packet->pts = (int64_t)(CMTimeGetSeconds(pts));
    packet->dts = (int64_t)(CMTimeGetSeconds(dts));
    packet->data = d_ptr->packet_data;
    packet->keyframe = keyframe;

    // VideoToolbox produces packets with priority lower than the RTMP code
    // expects, which causes it to be unable to recover from frame drops.
    // Fix this by manually adjusting the priority.
    uint8_t *start = d_ptr->packet_data->data();
    uint8_t *end = start + d_ptr->packet_data->size();

    start = (uint8_t *)obs_avc_find_startcode(start, end);
    while (true) {
        while (start < end && !*(start++))
            ;

        if (start == end)
            break;

        const int type = start[0] & 0x1F;
        if (type == OBS_NAL_SLICE_IDR ||
            type == OBS_NAL_SLICE) {
            uint8_t prev_type = (start[0] >> 5) & 0x3;
            start[0] &= ~(3 << 5);

            if (type == OBS_NAL_SLICE_IDR)
                start[0] |= OBS_NAL_PRIORITY_HIGHEST
                            << 5;
            else if (type == OBS_NAL_SLICE &&
                     prev_type !=
                         OBS_NAL_PRIORITY_DISPOSABLE)
                start[0] |= OBS_NAL_PRIORITY_HIGH << 5;
            else
                start[0] |= prev_type << 5;
        }

        start = (uint8_t *)obs_avc_find_startcode(start, end);
    }

    send_off(packet);

    CFRelease(buffer);
    return true;
}

#if TARGET_PLATFORM == PLATFORM_IOS

bool videotoolbox_encoder::create_gl_cvpixelbuffer()
{
    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, d_ptr->gl_ctx, NULL, &d_ptr->texture_cache);

    if (err)
        return false;

    auto empty = CFDictionaryCreate(kCFAllocatorDefault, // our empty IOSurface properties dictionary
                                    NULL,
                                    NULL,
                                    0,
                                    &kCFTypeDictionaryKeyCallBacks,
                                    &kCFTypeDictionaryValueCallBacks);
    auto attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 1,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);
    err = CVPixelBufferCreate(kCFAllocatorDefault, d_ptr->width, d_ptr->height,
                              kCVPixelFormatType_32BGRA, attrs, &d_ptr->target);
    CFRelease(empty);
    CFRelease(attrs);

    if (err)
        return false;

    err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                       d_ptr->texture_cache,
                                                       d_ptr->target,
                                                       nullptr, // texture attributes
                                                       GL_TEXTURE_2D,
                                                       GL_RGBA, // opengl format
                                                       d_ptr->width,
                                                       d_ptr->height,
                                                       GL_BGRA, // native iOS format
                                                       GL_UNSIGNED_BYTE,
                                                       0,
                                                       &d_ptr->texture);


    return err == kCVReturnSuccess;
}

bool videotoolbox_encoder::create_gl_fbo()
{
    glBindTexture(CVOpenGLESTextureGetTarget(d_ptr->texture), CVOpenGLESTextureGetName(d_ptr->texture));

    // Set up filter and wrap modes for this texture object
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // Allocate a texture image with which we can render to
    // Pass NULL for the data parameter since we don't need to load image data.
    //     We will be generating the image by rendering to this texture
    glTexImage2D(GL_TEXTURE_2D,
                 0, GL_RGBA,
                 d_ptr->width, d_ptr->height,
                 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);

    glGenRenderbuffers(1, &d_ptr->depth_buffer);
    glBindRenderbuffer(GL_RENDERBUFFER, d_ptr->depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, d_ptr->width, d_ptr->height);

    glGenFramebuffers(1, &d_ptr->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, d_ptr->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, CVOpenGLESTextureGetName(d_ptr->texture), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, d_ptr->depth_buffer);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        blog(LOG_WARNING, "failed to make complete framebuffer object %d", glCheckFramebufferStatus(GL_FRAMEBUFFER));
        return false;
    }

    return true;
}

bool videotoolbox_encoder::create_texture_encode_resource()
{
    gl_make_current(d_ptr->gl_ctx);
    d_ptr->texture_drawer = std::make_unique<gs_simple_texture_drawer>();
    bool success =  create_gl_cvpixelbuffer() && create_gl_fbo() && d_ptr->texture_drawer;
    gl_done_current();

    return success;
}

void videotoolbox_encoder::draw_video_frame_texture(unsigned int tex_id)
{
    gl_make_current(d_ptr->gl_ctx);

    glBindFramebuffer(GL_FRAMEBUFFER, d_ptr->fbo);
    glViewport(0, 0, d_ptr->width, d_ptr->height);

    d_ptr->texture_drawer->draw_texture(tex_id);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();

    gl_done_current();
}

#endif

bool videotoolbox_encoder::i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void (std::shared_ptr<encoder_packet>)> send_off)
{
#if TARGET_PLATFORM == PLATFORM_IOS
    if (!d_ptr->texture_encode_initialized) {
        if (!create_texture_encode_resource())
            blog(LOG_WARNING, "videotoolbox: create_texture_encode_resource error");

        d_ptr->texture_encode_initialized = true;
    }

    if (!d_ptr->fbo)
        return false;

    draw_video_frame_texture(*((int *)frame->data[0]));
#endif

    d_ptr->packet_data->clear();

    CMTime dur = CMTimeMake(d_ptr->fps_den, d_ptr->fps_num);
    CMTime off = CMTimeMultiply(dur, 2);
    CMTime pts = CMTimeMake(frame->pts, d_ptr->fps_num);

#if TARGET_PLATFORM == PLATFORM_MAC
    CVPixelBufferRef pixbuf = nullptr;
    if (!get_cached_pixel_buffer(d_ptr->session, d_ptr->colorspace, &pixbuf)) {
        blog(LOG_ERROR, "videotoolbox: Unable to create pixel buffer");
        return false;
    }

    auto code = CVPixelBufferLockBaseAddress(pixbuf, 0);
    if (code != noErr) {
        return false;
    }

    for (int i = 0; i < MAX_AV_PLANES; i++) {
        if (frame->data[i] == nullptr)
            break;
        uint8_t *p = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(
            pixbuf, i);
        uint8_t *f = frame->data[i];
        size_t plane_linesize =
            CVPixelBufferGetBytesPerRowOfPlane(pixbuf, i);
        size_t plane_height = CVPixelBufferGetHeightOfPlane(pixbuf, i);

        for (size_t j = 0; j < plane_height; j++) {
            memcpy(p, f, frame->linesize[i]);
            p += plane_linesize;
            f += frame->linesize[i];
        }
    }

    code = CVPixelBufferUnlockBaseAddress(pixbuf, 0);
    if (code != noErr) {
        return false;
    }

    code = VTCompressionSessionEncodeFrame(d_ptr->session, pixbuf, pts, dur, nullptr, pixbuf, nullptr);
    if (code != noErr) {
        return false;
    }
#else
    auto code = VTCompressionSessionEncodeFrame(d_ptr->session, d_ptr->target, pts, dur, nullptr, nullptr, nullptr);
    if (code != noErr) {
        return false;
    }
#endif

    CMSampleBufferRef buffer = (CMSampleBufferRef)CMSimpleQueueDequeue(d_ptr->queue);

    if (buffer == nullptr)
        return true;

    return parse_sample(buffer, packet, off, send_off);
}

bool videotoolbox_encoder::i_get_extra_data(uint8_t **extra_data, size_t *size)
{
    *extra_data = d_ptr->extra_data.data();
    *size = d_ptr->extra_data.size();
    return true;
}

bool videotoolbox_encoder::i_get_sei_data(uint8_t **sei_data, size_t *size)
{
    *sei_data = d_ptr->sei.data();
    *size = d_ptr->sei.size();
    return true;
}

void videotoolbox_encoder::i_get_video_info(video_scale_info *info)
{
    info->format = video_format::VIDEO_FORMAT_NV12;
}

bool videotoolbox_encoder::i_gpu_encode_available()
{
#if TARGET_PLATFORM == PLATFORM_MAC
    return false;
#else
    return true;
#endif
}

void videotoolbox_encoder::i_update_encode_bitrate(int bitrate)
{
    uint32_t old_bitrate = d_ptr->bitrate;
    d_ptr->bitrate = bitrate;

    if (old_bitrate == d_ptr->bitrate)
        return;

    OSStatus code = session_set_bitrate(d_ptr->session);
    if (code != noErr)
        blog(LOG_WARNING, "videotoolbox: Failed to set bitrate to session");

    dump_encoder_info();
}

#endif
