#include "lite-obs/lite_source.h"
#include "lite-obs/util/circlebuf.h"
#include "lite-obs/util/log.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/media-io/audio_resampler.h"
#include "lite-obs/media-io/audio_output.h"
#include "lite-obs/media-io/video_info.h"
#include "lite-obs/media-io/video_matrices.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/graphics/gs_texture.h"
#include "lite-obs/graphics/gs_texture_render.h"
#include "lite-obs/graphics/gs_subsystem.h"
#include "lite-obs/graphics/gs_program.h"
#include <mutex>
#include <list>
#include <inttypes.h>

/* maximum timestamp variance in nanoseconds */
#define MAX_TS_VAR 2000000000ULL
#define MAX_AUDIO_SIZE (AUDIO_OUTPUT_FRAMES * sizeof(float))

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2)
{
    return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

/* unless the value is 3+ hours worth of frames, this won't overflow */
static inline uint64_t conv_frames_to_time(const size_t sample_rate,
                                           const size_t frames)
{
    if (!sample_rate)
        return 0;

    return (uint64_t)frames * 1000000000ULL / (uint64_t)sample_rate;
}

static inline size_t conv_time_to_frames(const size_t sample_rate, const uint64_t duration)
{
    return (size_t)(duration * (uint64_t)sample_rate / 1000000000ULL);
}

static inline size_t get_buf_placement(uint32_t sample_rate, uint64_t offset)
{
    return (size_t)(offset * (uint64_t)sample_rate / 1000000000ULL);
}

static inline bool close_float(float f1, float f2, float precision)
{
    return fabsf(f1 - f2) <= precision;
}

static void scaled_to(uint32_t src_width, uint32_t src_height,
                      uint32_t target_width, uint32_t target_height, source_aspect_ratio_mode mode,
                      uint32_t &out_width, uint32_t &out_height) {
    if (mode == source_aspect_ratio_mode::Ignore_Aspect_Ratio || src_width == 0 || src_height == 0) {
        out_width = target_width;
        out_height = target_height;
    } else {
        bool useHeight = false;
        auto rw = target_height * src_width / src_height;
        if (mode == source_aspect_ratio_mode::Keep_Aspect_Ratio) {
            useHeight = (rw <= target_width);
        } else { // mode == source_aspect_ratio_mode::Keep_Aspect_Ratio_By_Expanding
            useHeight = (rw >= target_width);
        }
        if (useHeight) {
            out_width = rw;
            out_height = target_height;
        } else {
            out_width = target_width;
            out_height = target_width * src_height / src_width;
        }
    }
}

static void calc_size(uint32_t tex_width, uint32_t tex_height,
                      uint32_t box_width, uint32_t box_height,
                      uint32_t &out_width, uint32_t &out_height)
{
    float origin_ratio = (float) tex_width / (float)tex_height;
    float target_ratio = (float)box_width / (float)box_height;
    if (origin_ratio > target_ratio) {
        out_height = tex_height;
        out_width = (uint32_t)(out_height * target_ratio);
    } else {
        out_width = tex_width;
        out_height = (uint32_t)(out_width / target_ratio);
    }
}

enum convert_type {
    CONVERT_NONE,
    CONVERT_NV12,
    CONVERT_420,
    CONVERT_420_A,
    CONVERT_422,
    CONVERT_422_A,
    CONVERT_422_PACK,
    CONVERT_444,
    CONVERT_444_A,
    CONVERT_444_A_PACK,
    CONVERT_800,
    CONVERT_RGB_LIMITED,
    CONVERT_BGR3,
};

static inline enum convert_type get_convert_type(video_format format,
                                                 bool full_range)
{
    switch (format) {
    case video_format::VIDEO_FORMAT_I420:
        return CONVERT_420;
    case video_format::VIDEO_FORMAT_NV12:
        return CONVERT_NV12;
    case video_format::VIDEO_FORMAT_I444:
        return CONVERT_444;
    case video_format::VIDEO_FORMAT_I422:
        return CONVERT_422;

    case video_format::VIDEO_FORMAT_YVYU:
    case video_format::VIDEO_FORMAT_YUY2:
    case video_format::VIDEO_FORMAT_UYVY:
        return CONVERT_422_PACK;

    case video_format::VIDEO_FORMAT_Y800:
        return CONVERT_800;

    case video_format::VIDEO_FORMAT_NONE:
    case video_format::VIDEO_FORMAT_RGBA:
    case video_format::VIDEO_FORMAT_BGRA:
    case video_format::VIDEO_FORMAT_BGRX:
        return full_range ? CONVERT_NONE : CONVERT_RGB_LIMITED;

    case video_format::VIDEO_FORMAT_BGR3:
        return CONVERT_BGR3;

    case video_format::VIDEO_FORMAT_I40A:
        return CONVERT_420_A;

    case video_format::VIDEO_FORMAT_I42A:
        return CONVERT_422_A;

    case video_format::VIDEO_FORMAT_YUVA:
        return CONVERT_444_A;

    case video_format::VIDEO_FORMAT_AYUV:
        return CONVERT_444_A_PACK;
    }

    return CONVERT_NONE;
}

static inline gs_color_format convert_video_format(video_format format)
{
    switch (format) {
    case video_format::VIDEO_FORMAT_RGBA:
        return gs_color_format::GS_RGBA;
    case video_format::VIDEO_FORMAT_BGRA:
    case video_format::VIDEO_FORMAT_I40A:
    case video_format::VIDEO_FORMAT_I42A:
    case video_format::VIDEO_FORMAT_YUVA:
    case video_format::VIDEO_FORMAT_AYUV:
        return gs_color_format::GS_BGRA;
    default:
        return gs_color_format::GS_BGRX;
    }
}

static const char *select_conversion_technique(video_format format, bool full_range)
{
    switch (format) {
    case video_format::VIDEO_FORMAT_UYVY:
        return "Convert_UYVY_Reverse";

    case video_format::VIDEO_FORMAT_YUY2:
        return "Convert_YUY2_Reverse";

    case video_format::VIDEO_FORMAT_YVYU:
        return "Convert_YVYU_Reverse";

    case video_format::VIDEO_FORMAT_I420:
        return "Convert_I420_Reverse";

    case video_format::VIDEO_FORMAT_NV12:
        return "Convert_NV12_Reverse";

    case video_format::VIDEO_FORMAT_I444:
        return "Convert_I444_Reverse";

    case video_format::VIDEO_FORMAT_Y800:
        return full_range ? "Convert_Y800_Full" : "Convert_Y800_Limited";

    case video_format::VIDEO_FORMAT_BGR3:
        return full_range ? "Convert_BGR3_Full" : "Convert_BGR3_Limited";

    case video_format::VIDEO_FORMAT_I422:
        return "Convert_I422_Reverse";

    case video_format::VIDEO_FORMAT_I40A:
        return "Convert_I40A_Reverse";

    case video_format::VIDEO_FORMAT_I42A:
        return "Convert_I42A_Reverse";

    case video_format::VIDEO_FORMAT_YUVA:
        return "Convert_YUVA_Reverse";

    case video_format::VIDEO_FORMAT_AYUV:
        return "Convert_AYUV_Reverse";

    case video_format::VIDEO_FORMAT_BGRA:
    case video_format::VIDEO_FORMAT_BGRX:
    case video_format::VIDEO_FORMAT_RGBA:
    case video_format::VIDEO_FORMAT_NONE:
        if (full_range)
            assert(false && "No conversion requested");
        else
            return "Convert_RGB_Limited";
        break;
    }
    return NULL;
}

struct audio_cb_info {
    obs_source_audio_capture callback{};
    void *param{};
};

struct async_frame {
    std::shared_ptr<lite_source::lite_obs_source_video_frame> frame{};
    long unused_count{};
    bool used{};
};

struct lite_source_private
{
    std::weak_ptr<lite_obs_core_video> core_video{};
    std::weak_ptr<lite_obs_core_audio> core_audio{};

    std::unique_ptr<lite_source::lite_obs_source_video_frame> video_frame{};
    video_colorspace cur_space{};
    video_range_type cur_range{};
    source_type type{};

    /* timing (if video is present, is based upon video) */
    volatile bool timing_set{};
    volatile uint64_t timing_adjust{};
    uint64_t resample_offset{};
    uint64_t last_audio_ts{};
    uint64_t next_audio_ts_min{};
    uint64_t next_audio_sys_ts_min{};
    uint64_t last_frame_ts{};
    uint64_t last_sys_timestamp{};
    bool async_rendered{};

    /* audio */
    bool audio_failed{};
    bool audio_pending{};
    bool pending_stop{};
    bool audio_active{};
    bool user_muted{};
    bool muted{};
    uint64_t audio_ts{};
    circlebuf audio_input_buf[MAX_AUDIO_CHANNELS]{};
    size_t last_audio_input_buf_size{};
    float *audio_output_buf[MAX_AUDIO_MIXES][MAX_AUDIO_CHANNELS]{};
    resample_info sample_info{};
    std::shared_ptr<audio_resampler> resampler{};
    std::mutex audio_buf_mutex;
    std::mutex audio_cb_mutex;
    std::list<audio_cb_info> audio_cb_list{};
    lite_source::lite_obs_source_audio_frame audio_data{};
    size_t audio_storage_size{};
    uint32_t audio_mixers{};
    float user_volume{};
    float volume{};
    int64_t sync_offset{};
    int64_t last_sync_offset{};

    /* async video data */
    std::vector<std::shared_ptr<gs_texture>> async_textures{};
    std::shared_ptr<gs_texture_render> async_texrender{};
    std::shared_ptr<lite_source::lite_obs_source_video_frame> cur_async_frame{};
    bool async_gpu_conversion{};
    bool correct_pixel_render{}; // opengl es not support bgra, first convert to rgba, then do correct in render
    enum video_format async_format{};
    bool async_full_range{};
    enum video_format async_cache_format{};
    bool async_cache_full_range{};
    enum gs_color_format async_texture_formats[MAX_AV_PLANES]{};
    int async_channel_count{};
    bool async_flip{};
    bool async_flip_h{};
    bool async_active{};
    bool async_update_texture{};
    std::list<std::shared_ptr<async_frame>> async_cache{};
    std::list<std::shared_ptr<lite_source::lite_obs_source_video_frame>> async_frames{};
    std::mutex async_mutex{};
    uint32_t async_width{};
    uint32_t async_height{};
    uint32_t async_cache_width{};
    uint32_t async_cache_height{};
    uint32_t async_convert_width[MAX_AV_PLANES]{};
    uint32_t async_convert_height[MAX_AV_PLANES]{};

    /* sync video data */
    std::shared_ptr<gs_texture> sync_texture{};
    std::mutex sync_mutex{};

    struct transform_info {
        glm::vec3 pos{0};
        glm::vec3 scale{1};
    };

    struct render_box_info {
        bool enabled{};
        int x{};
        int y{};
        int width{};
        int height{};
        source_aspect_ratio_mode mode{};
    };

    std::atomic_bool should_update_tranform{};
    glm::mat4x4 draw_transform{1};
    transform_info source_transform;
    std::shared_ptr<gs_texture_render> item_render{};
    render_box_info source_render_box;

    lite_source_private(source_type t) {
        video_frame = std::make_unique<lite_source::lite_obs_source_video_frame>();
        type = t;
        user_volume = 1.0f;
        volume = 1.0f;
        sync_offset = 0;
        audio_active = true;
        audio_mixers = 0xFF;

        async_textures.resize(MAX_AV_PLANES);

        if (type & source_type::Source_Audio){
            size_t size = sizeof(float) * AUDIO_OUTPUT_FRAMES * MAX_AUDIO_CHANNELS * MAX_AUDIO_MIXES;
            float *ptr = (float *)malloc(size);
            memset(ptr, 0, size);

            for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
                size_t mix_pos = mix * AUDIO_OUTPUT_FRAMES * MAX_AUDIO_CHANNELS;

                for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
                    audio_output_buf[mix][i] = ptr + mix_pos + AUDIO_OUTPUT_FRAMES * i;
                }
            }
        }
    }

    ~lite_source_private() {
        gs_enter_contex(core_video.lock()->graphics());

        if (async_texrender)
            async_texrender.reset();

        async_textures.clear();

        gs_leave_context();

        for (int i = 0; i < MAX_AV_PLANES; i++)
            free((void *)audio_data.data[i]);
        for (int i = 0; i < MAX_AUDIO_CHANNELS; i++)
            circlebuf_free(&audio_input_buf[i]);

        resampler.reset();
        free(audio_output_buf[0][0]);

        audio_cb_list.clear();

        async_cache.clear();
        async_frames.clear();
        cur_async_frame.reset();
    }
};
std::recursive_mutex lite_source::sources_mutex{};
std::map<uintptr_t, std::map<uintptr_t, std::pair<source_type,std::shared_ptr<lite_source>>>> lite_source::sources{};

lite_source::lite_source(source_type type, std::shared_ptr<lite_obs_core_video> c_v, std::shared_ptr<lite_obs_core_audio> c_a)
{
    d_ptr = std::make_unique<lite_source_private>(type);
    d_ptr->core_video = c_v;
    d_ptr->core_audio = c_a;
}

lite_source::~lite_source()
{
    blog(LOG_DEBUG, "source destroyed");
}

source_type lite_source::lite_source_type()
{
    return d_ptr->type;
}

bool lite_source::audio_pending()
{
    return d_ptr->audio_pending;
}

uint64_t lite_source::audio_ts()
{
    return d_ptr->audio_ts;
}

void lite_source::reset_resampler(const lite_obs_source_audio_frame &audio)
{
    auto core_audio = d_ptr->core_audio.lock();
    if (!core_audio)
        return;

    auto audio_output_info = core_audio->core_audio()->audio_output_get_info();

    struct resample_info output_info;
    output_info.format = audio_output_info->format;
    output_info.samples_per_sec = audio_output_info->samples_per_sec;
    output_info.speakers = audio_output_info->speakers;

    d_ptr->sample_info.format = audio.format;
    d_ptr->sample_info.samples_per_sec = audio.samples_per_sec;
    d_ptr->sample_info.speakers = audio.speakers;

    d_ptr->resampler.reset();
    d_ptr->resample_offset = 0;

    if (d_ptr->sample_info.samples_per_sec == audio_output_info->samples_per_sec &&
        d_ptr->sample_info.format == audio_output_info->format &&
        d_ptr->sample_info.speakers == audio_output_info->speakers) {
        d_ptr->audio_failed = false;
        return;
    }

    d_ptr->resampler = std::make_shared<audio_resampler>();
    d_ptr->resampler->create(&output_info, &d_ptr->sample_info);

    d_ptr->audio_failed = d_ptr->resampler == NULL;
    if (d_ptr->resampler == NULL)
        blog(LOG_ERROR, "creation of resampler failed");
}

bool lite_source::copy_audio_data(const uint8_t *const data[], uint32_t frames, uint64_t ts)
{
    auto core_audio = d_ptr->core_audio.lock();
    if (!core_audio)
        return false;

    size_t planes = core_audio->core_audio()->audio_output_get_planes();;
    size_t blocksize = core_audio->core_audio()->audio_output_get_block_size();
    size_t size = (size_t)frames * blocksize;
    bool resize = d_ptr->audio_storage_size < size;

    d_ptr->audio_data.frames = frames;
    d_ptr->audio_data.timestamp = ts;

    for (size_t i = 0; i < planes; i++) {
        /* ensure audio storage capacity */
        if (resize) {
            free((void *)d_ptr->audio_data.data[i]);
            d_ptr->audio_data.data[i] = (uint8_t *)malloc(size);
        }

        memcpy((void *)d_ptr->audio_data.data[i], data[i], size);
    }

    if (resize)
        d_ptr->audio_storage_size = size;

    return true;
}

bool lite_source::process_audio(const lite_obs_source_audio_frame &audio)
{
    if (d_ptr->sample_info.samples_per_sec != audio.samples_per_sec ||
        d_ptr->sample_info.format != audio.format ||
        d_ptr->sample_info.speakers != audio.speakers)
        reset_resampler(audio);

    if (d_ptr->audio_failed)
        return false;

    uint32_t frames = audio.frames;
    bool ret = false;
    if (d_ptr->resampler) {
        uint8_t *output[MAX_AV_PLANES];
        memset(output, 0, sizeof(output));

        d_ptr->resampler->do_resample(output, &frames, &d_ptr->resample_offset, audio.data, audio.frames);

        ret = copy_audio_data((const uint8_t *const *)output, frames, audio.timestamp);
    } else {
        ret = copy_audio_data(audio.data, audio.frames, audio.timestamp);
    }

    return ret;
}

/* maximum buffer size */
#define MAX_BUF_SIZE (1000 * AUDIO_OUTPUT_FRAMES * sizeof(float))

/* time threshold in nanoseconds to ensure audio timing is as seamless as
 * possible */
#define TS_SMOOTHING_THRESHOLD 70000000ULL

void lite_source::reset_audio_data(uint64_t os_time)
{
    for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        if (d_ptr->audio_input_buf[i].size)
            circlebuf_pop_front(&d_ptr->audio_input_buf[i], NULL, d_ptr->audio_input_buf[i].size);
    }

    d_ptr->last_audio_input_buf_size = 0;
    d_ptr->audio_ts = os_time;
    d_ptr->next_audio_sys_ts_min = os_time;
}

void lite_source::reset_audio_timing(uint64_t timestamp, uint64_t os_time)
{
    d_ptr->timing_set = true;
    d_ptr->timing_adjust = os_time - timestamp;
}

void lite_source::handle_ts_jump(uint64_t expected, uint64_t ts, uint64_t diff, uint64_t os_time)
{
    blog(LOG_DEBUG,
         "Timestamp for source jumped by '%" PRIu64 "', "
         "expected value %" PRIu64 ", input value %" PRIu64, diff, expected, ts);

    d_ptr->audio_buf_mutex.lock();
    reset_audio_timing(ts, os_time);
    d_ptr->audio_buf_mutex.unlock();
}

void lite_source::output_audio_data_internal(const audio_data *data)
{
    auto core_audio = d_ptr->core_audio.lock();
    if (!core_audio)
        return;

    size_t sample_rate = core_audio->core_audio()->audio_output_get_sample_rate();
    auto channels = core_audio->core_audio()->audio_output_get_channels();
    struct audio_data in = *data;
    uint64_t diff;
    uint64_t os_time = os_gettime_ns();
    int64_t sync_offset;
    bool using_direct_ts = false;
    bool push_back = false;

    /* detects 'directly' set timestamps as long as they're within
     * a certain threshold */
    if (uint64_diff(in.timestamp, os_time) < MAX_TS_VAR) {
        d_ptr->timing_adjust = 0;
        d_ptr->timing_set = true;
        using_direct_ts = true;
    }

    if (!d_ptr->timing_set) {
        reset_audio_timing(in.timestamp, os_time);

    } else if (d_ptr->next_audio_ts_min != 0) {
        diff = uint64_diff(d_ptr->next_audio_ts_min, in.timestamp);

        /* smooth audio if within threshold */
        if (diff > MAX_TS_VAR && !using_direct_ts)
            handle_ts_jump(d_ptr->next_audio_ts_min, in.timestamp, diff, os_time);
        else if (diff < TS_SMOOTHING_THRESHOLD) {
            in.timestamp = d_ptr->next_audio_ts_min;
        }
    }

    d_ptr->last_audio_ts = in.timestamp;
    d_ptr->next_audio_ts_min = in.timestamp + conv_frames_to_time(sample_rate, in.frames);

    in.timestamp += d_ptr->timing_adjust;

    d_ptr->audio_buf_mutex.lock();

    if (d_ptr->next_audio_sys_ts_min == in.timestamp) {
        push_back = true;

    } else if (d_ptr->next_audio_sys_ts_min) {
        diff = uint64_diff(d_ptr->next_audio_sys_ts_min, in.timestamp);

        if (diff < TS_SMOOTHING_THRESHOLD) {
            push_back = true;

            /* This typically only happens if used with async video when
         * audio/video start transitioning in to a timestamp jump.
         * Audio will typically have a timestamp jump, and then video
         * will have a timestamp jump.  If that case is encountered,
         * just clear the audio data in that small window and force a
         * resync.  This handles all cases rather than just looping. */
        } else if (diff > MAX_TS_VAR) {
            reset_audio_timing(data->timestamp, os_time);
            in.timestamp = data->timestamp + d_ptr->timing_adjust;
        }
    }

    sync_offset = d_ptr->sync_offset;
    in.timestamp += sync_offset;
    in.timestamp -= d_ptr->resample_offset;

    d_ptr->next_audio_sys_ts_min =
        d_ptr->next_audio_ts_min + d_ptr->timing_adjust;

    if (d_ptr->last_sync_offset != sync_offset) {
        if (d_ptr->last_sync_offset)
            push_back = false;
        d_ptr->last_sync_offset = sync_offset;
    }

    if (push_back && d_ptr->audio_ts)
        output_audio_push_back(&in, channels);
    else
        output_audio_place(&in, channels, sample_rate);

    d_ptr->audio_buf_mutex.unlock();
}

void lite_source::output_audio_push_back(const struct audio_data *in, size_t channels)
{
    size_t size = in->frames * sizeof(float);

    /* do not allow the circular buffers to become too big */
    if ((d_ptr->audio_input_buf[0].size + size) > MAX_BUF_SIZE)
        return;

    for (size_t i = 0; i < channels; i++)
        circlebuf_push_back(&d_ptr->audio_input_buf[i], in->data[i],
                            size);

    /* reset audio input buffer size to ensure that audio doesn't get
     * perpetually cut */
    d_ptr->last_audio_input_buf_size = 0;
}

void lite_source::output_audio_place(const struct audio_data *in, size_t channels, size_t sample_rate)
{
    size_t size = in->frames * sizeof(float);

    if (!d_ptr->audio_ts || in->timestamp < d_ptr->audio_ts)
        reset_audio_data(in->timestamp);

    size_t buf_placement = get_buf_placement((uint32_t)sample_rate, in->timestamp - d_ptr->audio_ts) * sizeof(float);

    /* do not allow the circular buffers to become too big */
    if ((buf_placement + size) > MAX_BUF_SIZE)
        return;

    for (size_t i = 0; i < channels; i++) {
        circlebuf_place(&d_ptr->audio_input_buf[i], buf_placement, in->data[i], size);
        circlebuf_pop_back(&d_ptr->audio_input_buf[i], NULL, d_ptr->audio_input_buf[i].size - (buf_placement + size));
    }

    d_ptr->last_audio_input_buf_size = 0;
}

void lite_source::lite_source_output_audio(const lite_obs_source_audio_frame &audio)
{
    if (!process_audio(audio))
        return;

    struct audio_data data;

    for (int i = 0; i < MAX_AV_PLANES; i++)
        data.data[i] = (uint8_t *)d_ptr->audio_data.data[i];

    data.frames = d_ptr->audio_data.frames;
    data.timestamp = d_ptr->audio_data.timestamp;

    output_audio_data_internal(&data);
}

float lite_source::get_source_volume()
{
    bool muted = d_ptr->muted;

    if (muted || close_float(d_ptr->volume, 0.0f, 0.0001f))
        return 0.0f;
    if (close_float(d_ptr->volume, 1.0f, 0.0001f))
        return 1.0f;

    return d_ptr->volume;
}

void lite_source::multiply_output_audio(size_t mix, size_t channels, float vol)
{
    float *out = d_ptr->audio_output_buf[mix][0];
    float *end = out + AUDIO_OUTPUT_FRAMES * channels;

    while (out < end)
        *(out++) *= vol;
}

void lite_source::apply_audio_volume(uint32_t mixers, size_t channels, size_t sample_rate)
{
    auto vol = get_source_volume();
    if (vol == 1.0f)
        return;

    if (vol == 0.0f || mixers == 0) {
        memset(d_ptr->audio_output_buf[0][0], 0, AUDIO_OUTPUT_FRAMES * sizeof(float) * MAX_AUDIO_CHANNELS * MAX_AUDIO_MIXES);
        return;
    }

    for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
        uint32_t mix_and_val = (1 << mix);
        if ((d_ptr->audio_mixers & mix_and_val) != 0 && (mixers & mix_and_val) != 0)
            multiply_output_audio(mix, channels, vol);
    }
}

void lite_source::audio_source_tick(uint32_t mixers, size_t channels, size_t sample_rate, size_t size)
{
    d_ptr->audio_buf_mutex.lock();

    if (d_ptr->audio_input_buf[0].size < size) {
        d_ptr->audio_pending = true;
        d_ptr->audio_buf_mutex.unlock();
        return;
    }

    for (size_t ch = 0; ch < channels; ch++)
        circlebuf_peek_front(&d_ptr->audio_input_buf[ch], d_ptr->audio_output_buf[0][ch], size);

    d_ptr->audio_buf_mutex.unlock();

    for (size_t mix = 1; mix < MAX_AUDIO_MIXES; mix++) {
        uint32_t mix_and_val = (1 << mix);

        if ((d_ptr->audio_mixers & mix_and_val) == 0 || (mixers & mix_and_val) == 0) {
            memset(d_ptr->audio_output_buf[mix][0], 0, size * channels);
            continue;
        }

        for (size_t ch = 0; ch < channels; ch++)
            memcpy(d_ptr->audio_output_buf[mix][ch], d_ptr->audio_output_buf[0][ch], size);
    }

    if ((d_ptr->audio_mixers & 1) == 0 || (mixers & 1) == 0)
        memset(d_ptr->audio_output_buf[0][0], 0, size * channels);

    apply_audio_volume(mixers, channels, sample_rate);
    d_ptr->audio_pending = false;
}

void lite_source::audio_render(uint32_t mixers, size_t channels, size_t sample_rate, size_t size)
{
    if (!d_ptr->audio_output_buf[0][0]) {
        d_ptr->audio_pending = true;
        return;
    }

    if (!d_ptr->audio_ts) {
        d_ptr->audio_pending = true;
        return;
    }

    audio_source_tick(mixers, channels, sample_rate, size);
}

bool lite_source::audio_buffer_insuffient(size_t sample_rate, uint64_t min_ts)
{
    size_t total_floats = AUDIO_OUTPUT_FRAMES;

    if (d_ptr->audio_pending || !d_ptr->audio_ts) {
        return false;
    }

    if (d_ptr->audio_ts != min_ts && d_ptr->audio_ts != (min_ts - 1)) {
        size_t start_point = conv_time_to_frames(sample_rate, d_ptr->audio_ts - min_ts);
        if (start_point >= AUDIO_OUTPUT_FRAMES)
            return false;

        total_floats -= start_point;
    }

    auto size = total_floats * sizeof(float);

    if (d_ptr->audio_input_buf[0].size < size) {
        d_ptr->audio_pending = true;
        return true;
    }

    return false;
}

void lite_source::mix_audio(struct audio_output_data *mixes, size_t channels, size_t sample_rate, struct ts_info *ts)
{
    d_ptr->audio_buf_mutex.lock();
    if (d_ptr->audio_output_buf[0][0] && d_ptr->audio_ts) {
        size_t total_floats = AUDIO_OUTPUT_FRAMES;
        size_t start_point = 0;

        if (d_ptr->audio_ts < ts->start || ts->end <= d_ptr->audio_ts)
            return;

        if (d_ptr->audio_ts != ts->start) {
            start_point = conv_time_to_frames(sample_rate, d_ptr->audio_ts - ts->start);
            if (start_point == AUDIO_OUTPUT_FRAMES)
                return;

            total_floats -= start_point;
        }

        for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
            for (size_t ch = 0; ch < channels; ch++) {
                float *mix = mixes[mix_idx].data[ch];
                float *aud = d_ptr->audio_output_buf[mix_idx][ch];
                float *end;

                mix += start_point;
                end = aud + total_floats;

                while (aud < end)
                    *(mix++) += *(aud++);
            }
        }
    }
    d_ptr->audio_buf_mutex.unlock();
}

bool lite_source::discard_if_stopped(size_t channels)
{
    size_t last_size;
    size_t size;

    last_size = d_ptr->last_audio_input_buf_size;
    size = d_ptr->audio_input_buf[0].size;

    if (!size)
        return false;

    /* if perpetually pending data, it means the audio has stopped,
         * so clear the audio data */
    if (last_size == size) {
        if (!d_ptr->pending_stop) {
            d_ptr->pending_stop = true;
            return true;
        }

        for (size_t ch = 0; ch < channels; ch++)
            circlebuf_pop_front(&d_ptr->audio_input_buf[ch], NULL, d_ptr->audio_input_buf[ch].size);

        d_ptr->pending_stop = false;
        d_ptr->audio_ts = 0;
        d_ptr->last_audio_input_buf_size = 0;
        return true;
    } else {
        d_ptr->last_audio_input_buf_size = size;
        return false;
    }
}

void lite_source::ignore_audio(size_t channels, size_t sample_rate)
{
    size_t num_floats = d_ptr->audio_input_buf[0].size / sizeof(float);

    if (num_floats) {
        for (size_t ch = 0; ch < channels; ch++)
            circlebuf_pop_front(&d_ptr->audio_input_buf[ch], NULL, d_ptr->audio_input_buf[ch].size);

        d_ptr->last_audio_input_buf_size = 0;
        d_ptr->audio_ts += (uint64_t)num_floats * 1000000000ULL / (uint64_t)sample_rate;
    }
}

void lite_source::discard_audio(int total_buffering_ticks, size_t channels, size_t sample_rate, struct ts_info *ts)
{
    std::lock_guard<std::mutex> lock(d_ptr->audio_buf_mutex);

    if (ts->end <= d_ptr->audio_ts) {
        return;
    }

    if (d_ptr->audio_ts < (ts->start - 1)) {
        if (d_ptr->audio_pending && d_ptr->audio_input_buf[0].size < MAX_AUDIO_SIZE && discard_if_stopped(channels))
            return;

        if (total_buffering_ticks == MAX_BUFFERING_TICKS)
            ignore_audio(channels, sample_rate);
        return;
    }

    size_t total_floats = AUDIO_OUTPUT_FRAMES;
    if (d_ptr->audio_ts != ts->start && d_ptr->audio_ts != (ts->start - 1)) {
        size_t start_point = conv_time_to_frames(sample_rate, d_ptr->audio_ts - ts->start);
        if (start_point == AUDIO_OUTPUT_FRAMES) {
            return;
        }

        total_floats -= start_point;
    }

    auto size = total_floats * sizeof(float);

    if (d_ptr->audio_input_buf[0].size < size) {
        if (discard_if_stopped(channels))
            return;

        d_ptr->audio_ts = ts->end;
        return;
    }

    for (size_t ch = 0; ch < channels; ch++)
        circlebuf_pop_front(&d_ptr->audio_input_buf[ch], NULL, size);

    d_ptr->last_audio_input_buf_size = 0;

    d_ptr->pending_stop = false;
    d_ptr->audio_ts = ts->end;
}

//video

#define MAX_UNUSED_FRAME_DURATION 5
void lite_source::clean_cache()
{
    auto iter = d_ptr->async_cache.begin();
    while (iter != d_ptr->async_cache.end()) {
        auto &af = *iter;
        if (!af->used) {
            if (++af->unused_count == MAX_UNUSED_FRAME_DURATION) {
                d_ptr->async_cache.erase(iter++);
                continue;
            }
        }
        iter++;
    }
}

void lite_source::free_async_cache()
{
    d_ptr->async_cache.clear();
    d_ptr->async_frames.clear();
    d_ptr->cur_async_frame.reset();
}

bool lite_source::async_texture_changed(const lite_obs_source_video_frame *frame)
{
    enum convert_type prev, cur;
    prev = get_convert_type(d_ptr->async_cache_format, d_ptr->async_cache_full_range);
    cur = get_convert_type(frame->format, frame->full_range);

    return d_ptr->async_cache_width != frame->width ||
           d_ptr->async_cache_height != frame->height || prev != cur;
}

#define MAX_ASYNC_FRAMES 30
void lite_source::output_video_internal(const lite_obs_source_video_frame *frame)
{
    if (!frame) {
        d_ptr->async_active = false;
        return;
    }

    std::lock_guard<std::mutex> locker(d_ptr->async_mutex);

    if (d_ptr->async_frames.size() >= MAX_ASYNC_FRAMES) {
        free_async_cache();
        d_ptr->last_frame_ts = 0;
        return;
    }

    if (async_texture_changed(frame)) {
        free_async_cache();
        d_ptr->async_cache_width = frame->width;
        d_ptr->async_cache_height = frame->height;
    }

    auto format = frame->format;
    d_ptr->async_cache_format = format;
    d_ptr->async_cache_full_range = frame->full_range;

    std::shared_ptr<lite_obs_source_video_frame> new_frame{};
    for (auto iter = d_ptr->async_cache.begin(); iter != d_ptr->async_cache.end(); iter++) {
        auto &af = *iter;
        if (!af->used) {
            new_frame = af->frame;
            new_frame->format = format;
            af->used = true;
            af->unused_count = 0;
            break;
        }
    }

    clean_cache();

    if (!new_frame) {
        auto new_af = std::make_shared<async_frame>();

        new_frame = std::make_shared<lite_source::lite_obs_source_video_frame>();
        new_frame->lite_obs_source_video_frame_init(format, frame->width, frame->height);
        new_af->frame = new_frame;
        new_af->used = true;
        new_af->unused_count = 0;

        d_ptr->async_cache.push_back(new_af);
    }

    new_frame->lite_obs_source_video_frame_copy(frame);
    d_ptr->async_frames.push_back(new_frame);
    d_ptr->async_active = true;
}

void lite_source::lite_source_output_video(const uint8_t *video_data[MAX_AV_PLANES], const int line_size[MAX_AV_PLANES], video_format format, video_range_type range, video_colorspace color_space, uint32_t width, uint32_t height)
{
    auto flip = line_size[0] < 0 && line_size[1] == 0;
    for (size_t i = 0; i < MAX_AV_PLANES; i++) {
        d_ptr->video_frame->data[i] = (uint8_t *)video_data[i];
        d_ptr->video_frame->linesize[i] = abs(line_size[i]);
    }

    if (flip)
        d_ptr->video_frame->data[0] -= d_ptr->video_frame->linesize[0] * (height - 1);

    if (format != d_ptr->video_frame->format || color_space != d_ptr->cur_space || range != d_ptr->cur_range) {
        d_ptr->video_frame->format = format;
        d_ptr->video_frame->full_range = range == video_range_type::VIDEO_RANGE_FULL;

        auto success = video_format_get_parameters(color_space, range,
                                                   d_ptr->video_frame->color_matrix,
                                                   d_ptr->video_frame->color_range_min,
                                                   d_ptr->video_frame->color_range_max);

        d_ptr->video_frame->format = format;
        d_ptr->cur_space = color_space;
        d_ptr->cur_range = range;

        if (!success) {
            d_ptr->video_frame->format = video_format::VIDEO_FORMAT_NONE;
            return;
        }
    }

    if (d_ptr->video_frame->format == video_format::VIDEO_FORMAT_NONE)
        return;

    d_ptr->video_frame->timestamp = os_gettime_ns();
    d_ptr->video_frame->width = width;
    d_ptr->video_frame->height = height;
    d_ptr->video_frame->flip = flip;

    d_ptr->video_frame->full_range = format_is_yuv(d_ptr->video_frame->format) ? d_ptr->video_frame->full_range : true;
    output_video_internal(d_ptr->video_frame.get());
}

void lite_source::lite_source_output_video(int texture_id, uint32_t texture_width, uint32_t texture_height)
{
    auto core_video = d_ptr->core_video.lock();
    if (!core_video) {
        blog(LOG_ERROR, "no core video!!!");
        return;
    }

    if (!core_video->graphics() || !core_video->graphics()->gs_texture_share_enabled()) {
        blog(LOG_INFO, "texture share not support due to unsupport opengl context");
        return;
    }
    std::lock_guard<std::mutex> locker(d_ptr->sync_mutex);

    if (d_ptr->type & source_type::Source_Async) {
        blog(LOG_ERROR, "async video not support texture input");
        return;
    }

    d_ptr->sync_texture = gs_texture_create_with_external(texture_id, texture_width, texture_height);
}

void lite_source::lite_source_clear_video()
{
    if (d_ptr->type & source_type::Source_Async)
        output_video_internal(NULL);
    else {
        std::lock_guard<std::mutex> locker(d_ptr->sync_mutex);
        d_ptr->sync_texture.reset();
    }
}

void lite_source::remove_async_frame(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    for (auto iter = d_ptr->async_cache.begin(); iter != d_ptr->async_cache.end(); iter++) {
        auto &f = *iter;
        if (f->frame == frame) {
            f->used = false;
            break;
        }
    }
}

bool lite_source::ready_async_frame(uint64_t sys_time)
{
    auto next_frame = d_ptr->async_frames.front();
    while (d_ptr->async_frames.size() > 1) {
        d_ptr->async_frames.pop_front();
        remove_async_frame(next_frame);
        next_frame = d_ptr->async_frames.front();
    }

    d_ptr->last_frame_ts = next_frame->timestamp;
    return true;
}

std::shared_ptr<lite_source::lite_obs_source_video_frame> lite_source::get_closest_frame(uint64_t sys_time)
{
    if (d_ptr->async_frames.empty())
        return nullptr;

    if (!d_ptr->last_frame_ts || ready_async_frame(sys_time)) {
        auto frame = d_ptr->async_frames.front();
        d_ptr->async_frames.pop_front();

        if (!d_ptr->last_frame_ts)
            d_ptr->last_frame_ts = frame->timestamp;

        return frame;
    }

    return nullptr;
}

bool lite_source::set_packed422_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width / 2;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_BGRA;
    d_ptr->async_channel_count = 1;
    return true;
}

bool lite_source::set_packed444_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_BGRA;
    d_ptr->async_channel_count = 1;
    return true;
}

bool lite_source::set_planar444_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width;
    d_ptr->async_convert_width[2] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height;
    d_ptr->async_convert_height[2] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 3;
    return true;
}

bool lite_source::set_planar444_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width;
    d_ptr->async_convert_width[2] = frame->width;
    d_ptr->async_convert_width[3] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height;
    d_ptr->async_convert_height[2] = frame->height;
    d_ptr->async_convert_height[3] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[3] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 4;
    return true;
}

bool lite_source::set_planar420_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width / 2;
    d_ptr->async_convert_width[2] = frame->width / 2;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height / 2;
    d_ptr->async_convert_height[2] = frame->height / 2;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 3;
    return true;
}

bool lite_source::set_planar420_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width / 2;
    d_ptr->async_convert_width[2] = frame->width / 2;
    d_ptr->async_convert_width[3] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height / 2;
    d_ptr->async_convert_height[2] = frame->height / 2;
    d_ptr->async_convert_height[3] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[3] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 4;
    return true;
}

bool lite_source::set_planar422_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width / 2;
    d_ptr->async_convert_width[2] = frame->width / 2;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height;
    d_ptr->async_convert_height[2] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 3;
    return true;
}

bool lite_source::set_planar422_alpha_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width / 2;
    d_ptr->async_convert_width[2] = frame->width / 2;
    d_ptr->async_convert_width[3] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height;
    d_ptr->async_convert_height[2] = frame->height;
    d_ptr->async_convert_height[3] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[2] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[3] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 4;
    return true;
}

bool lite_source::set_nv12_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_width[1] = frame->width / 2;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_convert_height[1] = frame->height / 2;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_texture_formats[1] = gs_color_format::GS_R8G8;
    d_ptr->async_channel_count = 2;
    return true;
}

bool lite_source::set_y800_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 1;
    return true;
}

bool lite_source::set_rgb_limited_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_texture_formats[0] = convert_video_format(frame->format);
    d_ptr->async_channel_count = 1;
    return true;
}

bool lite_source::set_bgr3_sizes(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    d_ptr->async_convert_width[0] = frame->width * 3;
    d_ptr->async_convert_height[0] = frame->height;
    d_ptr->async_texture_formats[0] = gs_color_format::GS_R8;
    d_ptr->async_channel_count = 1;
    return true;
}

bool lite_source::init_gpu_conversion(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    switch (get_convert_type(frame->format, frame->full_range)) {
    case CONVERT_422_PACK:
        return set_packed422_sizes(frame);

    case CONVERT_420:
        return set_planar420_sizes(frame);

    case CONVERT_422:
        return set_planar422_sizes(frame);

    case CONVERT_NV12:
        return set_nv12_sizes(frame);

    case CONVERT_444:
        return set_planar444_sizes(frame);

    case CONVERT_800:
        return set_y800_sizes(frame);

    case CONVERT_RGB_LIMITED:
        return set_rgb_limited_sizes(frame);

    case CONVERT_BGR3:
        return set_bgr3_sizes(frame);

    case CONVERT_420_A:
        return set_planar420_alpha_sizes(frame);

    case CONVERT_422_A:
        return set_planar422_alpha_sizes(frame);

    case CONVERT_444_A:
        return set_planar444_alpha_sizes(frame);

    case CONVERT_444_A_PACK:
        return set_packed444_alpha_sizes(frame);

    case CONVERT_NONE:
        assert(false && "No conversion requested");
        break;
    }
    return false;
}

//opengl es 2 does not support bgra pixel format.
bool lite_source::set_async_texture_size(const std::shared_ptr<lite_obs_source_video_frame> &frame)
{
    auto cur = get_convert_type(frame->format, frame->full_range);

    if (d_ptr->async_width == frame->width &&
        d_ptr->async_height == frame->height &&
        d_ptr->async_format == frame->format &&
        d_ptr->async_full_range == frame->full_range)
        return true;

    d_ptr->async_width = frame->width;
    d_ptr->async_height = frame->height;
    d_ptr->async_format = frame->format;
    d_ptr->async_full_range = frame->full_range;

    gs_enter_contex(d_ptr->core_video.lock()->graphics());

    for (size_t c = 0; c < MAX_AV_PLANES; c++) {
        d_ptr->async_textures[c].reset();
    }

    d_ptr->async_texrender.reset();

    auto format = convert_video_format(frame->format);
    d_ptr->correct_pixel_render = format == gs_color_format::GS_BGRA || format == gs_color_format::GS_BGRX;
    const bool async_gpu_conversion = (cur != CONVERT_NONE) && init_gpu_conversion(frame);
    d_ptr->async_gpu_conversion = async_gpu_conversion;
    if (async_gpu_conversion) {
        d_ptr->async_texrender = std::make_shared<gs_texture_render>(format, gs_zstencil_format::GS_ZS_NONE);

        for (int c = 0; c < d_ptr->async_channel_count; ++c)
            d_ptr->async_textures[c] = gs_texture_create(d_ptr->async_convert_width[c], d_ptr->async_convert_height[c], d_ptr->async_texture_formats[c], NULL, GS_DYNAMIC);
    } else {
        d_ptr->async_textures[0] = gs_texture_create(frame->width, frame->height, format, NULL, GS_DYNAMIC);
    }

    gs_leave_context();

    return d_ptr->async_textures[0] != NULL;
}

void lite_source::async_tick(uint64_t sys_time)
{
    d_ptr->async_mutex.lock();

    if (d_ptr->cur_async_frame) {
        remove_async_frame(d_ptr->cur_async_frame);
        d_ptr->cur_async_frame = nullptr;
    }

    d_ptr->cur_async_frame = get_closest_frame(sys_time);

    d_ptr->last_sys_timestamp = sys_time;
    d_ptr->async_mutex.unlock();

    if (d_ptr->cur_async_frame)
        d_ptr->async_update_texture = set_async_texture_size(d_ptr->cur_async_frame);

    d_ptr->async_rendered = false;
}

std::shared_ptr<lite_source::lite_obs_source_video_frame> lite_source::get_frame()
{
    std::shared_ptr<lite_source::lite_obs_source_video_frame> frame = nullptr;
    d_ptr->async_mutex.lock();
    frame = d_ptr->cur_async_frame;
    d_ptr->cur_async_frame = nullptr;
    d_ptr->async_mutex.unlock();

    return frame;
}

bool lite_source::update_async_texrender(const std::shared_ptr<lite_obs_source_video_frame> &frame, const std::vector<std::shared_ptr<gs_texture>> &tex, const std::shared_ptr<gs_texture_render> &texrender)
{
    texrender->gs_texrender_reset();

    switch (get_convert_type(frame->format, frame->full_range)) {
    case CONVERT_422_PACK:
    case CONVERT_800:
    case CONVERT_RGB_LIMITED:
    case CONVERT_BGR3:
    case CONVERT_420:
    case CONVERT_422:
    case CONVERT_NV12:
    case CONVERT_444:
    case CONVERT_420_A:
    case CONVERT_422_A:
    case CONVERT_444_A:
    case CONVERT_444_A_PACK:
        for (size_t c = 0; c < MAX_AV_PLANES; c++) {
            if (tex[c])
                tex[c]->gs_texture_set_image(frame->data[c], frame->linesize[c], false);
        }
        break;

    case CONVERT_NONE:
        assert(false && "No conversion requested");
        break;
    }

    uint32_t cx = d_ptr->async_width;
    uint32_t cy = d_ptr->async_height;

    const char *tech_name = select_conversion_technique(frame->format, frame->full_range);
    auto program = gs_get_effect_by_name(tech_name);
    if (!program)
        return false;

    const bool success = texrender->gs_texrender_begin(cx, cy);
    if (success) {
        if (tex[0])
            program->gs_effect_set_texture("image", tex[0]);
        if (tex[1])
            program->gs_effect_set_texture("image1", tex[1]);
        if (tex[2])
            program->gs_effect_set_texture("image2", tex[2]);
        if (tex[3])
            program->gs_effect_set_texture("image3", tex[3]);

        program->gs_effect_set_param("width", (float)cx);
        program->gs_effect_set_param("height", (float)cy);
        program->gs_effect_set_param("width_d2", (float)cx * 0.5f);
        program->gs_effect_set_param("height_d2", (float)cy * 0.5f);
        program->gs_effect_set_param("width_x2_i", 0.5f / (float)cx);

        glm::vec4 vec0 = {frame->color_matrix[0], frame->color_matrix[1], frame->color_matrix[2], frame->color_matrix[3]};
        glm::vec4 vec1 = {frame->color_matrix[4], frame->color_matrix[5], frame->color_matrix[6], frame->color_matrix[7]};
        glm::vec4 vec2 = {frame->color_matrix[8], frame->color_matrix[9], frame->color_matrix[10], frame->color_matrix[11]};

        program->gs_effect_set_param("color_vec0", vec0);
        program->gs_effect_set_param("color_vec1", vec1);
        program->gs_effect_set_param("color_vec2", vec2);
        if (!frame->full_range) {
            program->gs_effect_set_param("color_range_min", frame->color_range_min, sizeof(float) * 3);
            program->gs_effect_set_param("color_range_max", frame->color_range_max, sizeof(float) * 3);
        }

        gs_set_cur_effect(program);

        gs_enable_blending(false);

        gs_technique_begin();
        gs_draw(gs_draw_mode::GS_TRIS, 0, 3);
        gs_technique_end();

        gs_enable_blending(true);

        texrender->gs_texrender_end();
    }

    return success;
}

bool lite_source::update_async_textures(const std::shared_ptr<lite_obs_source_video_frame> &frame, const std::vector<std::shared_ptr<gs_texture>> &tex, const std::shared_ptr<gs_texture_render> &texrender)
{
    d_ptr->async_flip = frame->flip;
    d_ptr->async_flip_h = frame->flip_h;

    if (d_ptr->async_gpu_conversion && texrender)
        return update_async_texrender(frame, tex, texrender);

    auto type = get_convert_type(frame->format, frame->full_range);
    if (type == CONVERT_NONE) {
        tex[0]->gs_texture_set_image(frame->data[0], frame->linesize[0], false);
        return true;
    }

    return false;
}

void lite_source::update_async_video(uint64_t sys_time)
{
    bool async_video = d_ptr->type & source_type::Source_Async;
    if (!async_video)
        return;

    async_tick(sys_time);

    if (!d_ptr->async_rendered) {
        auto frame = get_frame();
        d_ptr->async_rendered = true;
        if (frame) {
            d_ptr->timing_adjust = sys_time - frame->timestamp;
            d_ptr->timing_set = true;

            if (d_ptr->async_update_texture) {
                update_async_textures(frame, d_ptr->async_textures, d_ptr->async_texrender);
                d_ptr->async_update_texture = false;
            }

            remove_async_frame(frame);
        }
    }
}

bool lite_source::render_crop_texture(const std::shared_ptr<gs_texture> &texture)
{
    if (!d_ptr->item_render)
        return false;

    auto width = texture->gs_texture_get_width();
    auto height = texture->gs_texture_get_height();
    uint32_t cx, cy;
    calc_size(width, height, d_ptr->source_render_box.width, d_ptr->source_render_box.height, cx, cy);
    d_ptr->item_render->gs_texrender_reset();
    if (!d_ptr->item_render->gs_texrender_begin(cx, cy))
        return false;

    glm::vec4 clear_color(1, 0, 0, 1);
    gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
    gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

    float cx_scale = (float)width / (float)cx;
    float cy_scale = (float)height / (float)cy;
    gs_matrix_translate(glm::vec3((int)(cx - width) / 2, (int)(height - cy) / 2, 0.0f));
    gs_matrix_scale(glm::vec3(cx_scale, cy_scale, 1.0f));

    auto program = gs_get_effect_by_name("Default_Draw");
    if(program) {
        gs_set_cur_effect(program);
        gs_technique_begin();
        program->gs_effect_set_texture("image", texture);
        gs_draw_sprite(texture, 0, 0, 0);
        gs_technique_end();
    }

    d_ptr->item_render->gs_texrender_end();

    auto mat = glm::mat4x4{1};
    mat = glm::translate(mat, glm::vec3(d_ptr->source_render_box.x, d_ptr->source_render_box.y, 0.0f));
    mat = glm::scale(mat, glm::vec3((float)d_ptr->source_render_box.width / (float)cx, (float)d_ptr->source_render_box.height / (float)cy, 1.0f));
    d_ptr->draw_transform = mat;

    return true;
}

void lite_source::render_texture(std::shared_ptr<gs_texture> texture)
{
    if (!texture)
        return;

    if (d_ptr->should_update_tranform) {
        do_update_transform(texture);
        d_ptr->should_update_tranform = false;
    }

    if (render_crop_texture(texture))
        texture = d_ptr->item_render->gs_texrender_get_texture();

    auto program = gs_get_effect_by_name("Default_Draw");
    if( !program)
        return;

    gs_matrix_push();
    gs_matrix_mul(d_ptr->draw_transform);

    gs_set_cur_effect(program);
    gs_technique_begin();

    program->gs_effect_set_texture("image", texture);

    uint32_t flag = 0;
    if (d_ptr->async_flip)
        flag |= GS_FLIP_V;
    if (d_ptr->async_flip_h)
        flag |= GS_FLIP_U;
    gs_draw_sprite(texture, flag, 0, 0);

    gs_technique_end();

    gs_matrix_pop();
}

void lite_source::async_render()
{
    if (d_ptr->async_textures[0] && d_ptr->async_active) {
        auto tex = d_ptr->async_textures[0];
        if (d_ptr->async_texrender)
            tex = d_ptr->async_texrender->gs_texrender_get_texture();
        render_texture(tex);
    }
}

void lite_source::render()
{
    if (d_ptr->type & source_type::Source_Async)
        async_render();
    else {
        std::lock_guard<std::mutex> locker(d_ptr->sync_mutex);
        render_texture(d_ptr->sync_texture);
    }
}

void lite_source::do_update_transform(const std::shared_ptr<gs_texture> &tex)
{
    if (d_ptr->source_render_box.enabled) {
        auto tex_width = tex->gs_texture_get_width();
        auto tex_height = tex->gs_texture_get_height();
        bool crop_enabled = d_ptr->source_render_box.mode == source_aspect_ratio_mode::Keep_Aspect_Ratio_By_Expanding
                            && ((int)tex_width != d_ptr->source_render_box.width || (int)tex_height != d_ptr->source_render_box.height);
        if (d_ptr->item_render && !crop_enabled)
            d_ptr->item_render.reset();
        else if (!d_ptr->item_render && crop_enabled)
            d_ptr->item_render = std::make_shared<gs_texture_render>(gs_color_format::GS_RGBA, gs_zstencil_format::GS_ZS_NONE);

        auto mat = glm::mat4x4{1};
        if (!crop_enabled) {
            if (d_ptr->source_render_box.mode == source_aspect_ratio_mode::Keep_Aspect_Ratio) {
                uint32_t cx, cy;
                scaled_to(tex_width, tex_height, d_ptr->source_render_box.width, d_ptr->source_render_box.height, d_ptr->source_render_box.mode, cx, cy);
                mat = glm::translate(mat, glm::vec3(d_ptr->source_render_box.x + (d_ptr->source_render_box.width - cx) / 2, d_ptr->source_render_box.y + (d_ptr->source_render_box.height -cy) / 2, 0.0f));
                mat = glm::scale(mat, glm::vec3((float)cx / (float)tex_width, (float)cy / (float)tex_height, 1.0f));
            } else {
                mat = glm::translate(mat, glm::vec3(d_ptr->source_render_box.x, d_ptr->source_render_box.y, 0.0f));
                if (d_ptr->source_render_box.mode == source_aspect_ratio_mode::Ignore_Aspect_Ratio) {
                    mat = glm::scale(mat, glm::vec3((float)d_ptr->source_render_box.width / (float)tex_width, (float)d_ptr->source_render_box.height / (float)tex_height, 1.0f));
                }
            }
        } else {
            mat = glm::translate(mat, d_ptr->source_transform.pos);
            mat = glm::scale(mat, d_ptr->source_transform.scale);
        }
        d_ptr->draw_transform = mat;
    }
}

void lite_source::lite_source_set_pos(float x, float y)
{
    d_ptr->source_transform.pos.x = x;
    d_ptr->source_transform.pos.y = y;
    d_ptr->should_update_tranform = true;
}

void lite_source::lite_source_set_scale(float width_scale, float height_scale)
{
    d_ptr->source_transform.scale.x = width_scale;
    d_ptr->source_transform.scale.y = height_scale;
    d_ptr->should_update_tranform = true;
}

void lite_source::lite_source_set_render_box(int x, int y, int width, int height, source_aspect_ratio_mode mode)
{
    if (!width || !height) {
        blog(LOG_INFO, "invalid render box settings, width:%d, height: %d.", width, height);
        return;
    }

    d_ptr->source_render_box = {true, x, y, width, height, mode};
    d_ptr->should_update_tranform = true;
}
