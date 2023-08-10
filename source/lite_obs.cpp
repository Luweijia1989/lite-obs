#include "lite-obs/lite_obs.h"

#include "lite-obs/lite_obs_defines.h"
#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/lite_source.h"
#include "lite-obs/output/null_output.h"
#include "lite-obs/output/lite_ffmpeg_mux.h"
#include "lite-obs/output/mpeg_ts_output.h"
#include "lite-obs/output/rtmp_stream_output.h"
#include "lite-obs/encoder/lite_h264_encoder.h"
#include "lite-obs/encoder/lite_aac_encoder.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/util/log.h"

#include <set>


struct lite_obs_media_source_private
{
    std::shared_ptr<lite_source> internal_source{};
};

lite_obs_media_source::lite_obs_media_source()
{
    d_ptr = new lite_obs_media_source_private;
}

lite_obs_media_source::~lite_obs_media_source()
{
    delete d_ptr;
}

void lite_obs_media_source::output_audio(const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate)
{
    lite_source::lite_obs_source_audio_frame af{};

    for (int i=0; i<MAX_AV_PLANES; i++)
        af.data[i] = audio_data[i];

    af.frames = frames;
    af.format = format;
    af.samples_per_sec = sample_rate;
    af.speakers = layout;
    af.timestamp = os_gettime_ns();

    d_ptr->internal_source->lite_source_output_audio(af);
}

void lite_obs_media_source::output_video(int texId, uint32_t width, uint32_t height)
{
    d_ptr->internal_source->lite_source_output_video(texId, width, height);
}

void lite_obs_media_source::set_pos(float x, float y)
{
    d_ptr->internal_source->lite_source_set_pos(x, y);
}

void lite_obs_media_source::set_scale(float s_w, float s_h)
{
    d_ptr->internal_source->lite_source_set_scale(s_w, s_h);
}

struct lite_obs_private
{
    std::shared_ptr<lite_obs_core_video> video{};
    std::shared_ptr<lite_obs_core_audio> audio{};

    std::shared_ptr<lite_obs_output> output{};
    std::shared_ptr<lite_obs_encoder> video_encoder{};
    std::shared_ptr<lite_obs_encoder> audio_encoder{};

    std::set<lite_obs_media_source *> sources;

    lite_obs_private() {
        auto ptr = reinterpret_cast<uintptr_t>(this);
        video = std::make_shared<lite_obs_core_video>(ptr);
        audio = std::make_shared<lite_obs_core_audio>(ptr);
    }

    ~lite_obs_private() {
        video_encoder.reset();
        audio_encoder.reset();

        video.reset();
        audio.reset();
    }
};

lite_obs::lite_obs()
{
    d_ptr = new lite_obs_private;
}

lite_obs::~lite_obs()
{
    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        auto &sources = lite_source::sources[reinterpret_cast<uintptr_t>(d_ptr)];
        sources.clear();
    }

    for (auto iter : d_ptr->sources) {
        delete iter;
    }
    d_ptr->sources.clear();

    if(d_ptr->output) {
        d_ptr->output->lite_obs_output_destroy();
        d_ptr->output.reset();
    }

    d_ptr->video->lite_obs_stop_video();
    d_ptr->audio->lite_obs_stop_audio();

    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        lite_source::sources.erase(reinterpret_cast<uintptr_t>(d_ptr));
    }

    delete d_ptr;
}

void lite_obs::set_log_callback(void (*log_callback)(int, const char *))
{
    base_set_log_handler(log_callback);
}

#define OBS_SIZE_MIN 2
#define OBS_SIZE_MAX (32 * 1024)

static inline bool size_valid(uint32_t width, uint32_t height)
{
    return (width >= OBS_SIZE_MIN && height >= OBS_SIZE_MIN && width <= OBS_SIZE_MAX && height <= OBS_SIZE_MAX);
}

int lite_obs::obs_reset_video(uint32_t width, uint32_t height, uint32_t fps)
{
    if (d_ptr->video->lite_obs_video_active())
        return OBS_VIDEO_CURRENTLY_ACTIVE;

    if (!size_valid(width, height))
        return OBS_VIDEO_INVALID_PARAM;

    d_ptr->video->lite_obs_stop_video();

    width &= 0xFFFFFFFC;
    height &= 0xFFFFFFFE;

    return d_ptr->video->lite_obs_start_video(width, height, fps);
}

bool lite_obs::obs_reset_audio(uint32_t sample_rate)
{
    return d_ptr->audio->lite_obs_start_audio(sample_rate);
}

bool lite_obs::lite_obs_start_output(std::string output_info, int vb, int ab, std::shared_ptr<lite_obs_output_callbak> callback)
{
    if (!d_ptr->output)
    {
        //auto output = std::make_shared<mpeg_ts_output>();
        //auto output = std::make_shared<lite_ffmpeg_mux>();
        auto output = std::make_shared<rtmp_stream_output>();
        output->i_set_output_info(output_info);
        if (!output->lite_obs_output_create(d_ptr->video, d_ptr->audio))
            return false;

        d_ptr->output = output;
        d_ptr->output->set_output_signal_callback(callback);

        d_ptr->video_encoder = std::make_shared<lite_h264_video_encoder>(vb, 0);
        d_ptr->video_encoder->lite_obs_encoder_set_core_video(d_ptr->video);
        d_ptr->audio_encoder = std::make_shared<lite_aac_encoder>(ab, 0);
        d_ptr->audio_encoder->lite_obs_encoder_set_core_audio(d_ptr->audio);
    }

    d_ptr->output->lite_obs_output_set_video_encoder(d_ptr->video_encoder);
    d_ptr->output->lite_obs_output_set_audio_encoder(d_ptr->audio_encoder, 0);
    return d_ptr->output->lite_obs_output_start();
}

void lite_obs::lite_obs_stop_output()
{
    if(!d_ptr->output)
        return;

    d_ptr->output->lite_obs_output_stop();
}

lite_obs_media_source *lite_obs::lite_obs_create_source(source_type type)
{
    auto source = std::make_shared<lite_source>(type, d_ptr->video, d_ptr->audio);
    auto source_wrapper = new lite_obs_media_source;
    source_wrapper->d_ptr->internal_source = source;
    d_ptr->sources.emplace(source_wrapper);

    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        auto &sources = lite_source::sources[reinterpret_cast<uintptr_t>(d_ptr)];
        sources.emplace(reinterpret_cast<uintptr_t>(source_wrapper), std::make_pair(type, source));
    }

    return source_wrapper;
}

void lite_obs::lite_obs_destroy_source(lite_obs_media_source *source)
{
    auto ptr = reinterpret_cast<uintptr_t>(source);

    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        auto &sources = lite_source::sources[reinterpret_cast<uintptr_t>(d_ptr)];
        sources.erase(ptr);
    }

    d_ptr->sources.erase(source);
    delete source;
}
