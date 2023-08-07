#include "lite-obs/lite_obs.h"

#include "lite-obs/lite_obs_defines.h"
#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/lite_source.h"
#include "lite-obs/output/null_output.h"
#include "lite-obs/output/lite_ffmpeg_mux.h"
#include "lite-obs/output/mpeg_ts_output.h"
#include "lite-obs/encoder/lite_h264_encoder.h"
#include "lite-obs/encoder/lite_aac_encoder.h"
#include "lite-obs/util/threading.h"
#include "lite-obs/util/log.h"

struct lite_obs_private
{
    std::shared_ptr<lite_obs_core_video> video{};
    std::shared_ptr<lite_obs_core_audio> audio{};

    std::shared_ptr<lite_obs_output> output{};
    std::shared_ptr<lite_obs_encoder> video_encoder{};
    std::shared_ptr<lite_obs_encoder> audio_encoder{};

    lite_obs_private() {
        auto ptr = reinterpret_cast<uintptr_t>(this);
        video = std::make_shared<lite_obs_core_video>(ptr);
        audio = std::make_shared<lite_obs_core_audio>(ptr);
    }

    ~lite_obs_private() {
        if(output) {
            output->lite_obs_output_destroy();
            output.reset();
        }

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
        auto output = std::make_shared<lite_ffmpeg_mux>();
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

uintptr_t lite_obs::lite_obs_create_source(source_type type)
{
    auto source = std::make_shared<lite_source>(type, d_ptr->video, d_ptr->audio);

    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        auto &sources = lite_source::sources[reinterpret_cast<uintptr_t>(d_ptr)];
        sources.emplace(reinterpret_cast<uintptr_t>(source.get()), std::make_pair(type, source));
    }

    return reinterpret_cast<uintptr_t>(source.get());
}

void lite_obs::lite_obs_destroy_source(uintptr_t source_ptr)
{
    std::shared_ptr<lite_source> cache_source{};
    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        auto &sources = lite_source::sources[reinterpret_cast<uintptr_t>(d_ptr)];
        if (sources.contains(source_ptr)) {
            cache_source = sources[source_ptr].second;
        }
        sources.erase(source_ptr);
    }

    cache_source.reset();
}

std::shared_ptr<lite_source> source_from_ptr(uintptr_t lite_obs_private_ptr, uintptr_t source_ptr)
{
    auto &sources = lite_source::sources[lite_obs_private_ptr];
    if (!sources.contains(source_ptr))
        return nullptr;

    return sources[source_ptr].second;
}

void lite_obs::lite_obs_source_output_audio(uintptr_t source_ptr, const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate)
{
    lite_source::lite_obs_source_audio_frame af{};

    for (int i=0; i<MAX_AV_PLANES; i++)
        af.data[i] = audio_data[i];

    af.frames = frames;
    af.format = format;
    af.samples_per_sec = sample_rate;
    af.speakers = layout;
    af.timestamp = os_gettime_ns();

    std::lock_guard<std::recursive_mutex> locker(lite_source::sources_mutex);
    auto source = source_from_ptr(reinterpret_cast<uintptr_t>(d_ptr), source_ptr);
    if(source)
        source->lite_source_output_audio(af);
}

void lite_obs::lite_obs_source_output_video(uintptr_t source_ptr, int texId, uint32_t width, uint32_t height)
{
    std::lock_guard<std::recursive_mutex> locker(lite_source::sources_mutex);
    auto source = source_from_ptr(reinterpret_cast<uintptr_t>(d_ptr), source_ptr);
    if(source)
        source->lite_source_output_video(texId, width, height);
}
