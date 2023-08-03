#include "lite-obs/lite_obs.h"

#include "lite-obs/obs-defs.h"
#include "lite-obs/lite_obs_core_video.h"
#include "lite-obs/lite_obs_core_audio.h"
#include "lite-obs/lite_source.h"
#include "lite-obs/output/null_output.h"
#include "lite-obs/output/lite_ffmpeg_mux.h"
#include "lite-obs/output/mpeg_ts_output.h"
#include "lite-obs/encoder/lite_h264_encoder.h"
#include "lite-obs/encoder/lite_aac_encoder.h"

struct lite_obs_private
{
    std::shared_ptr<lite_obs_core_video> video{};
    std::shared_ptr<lite_obs_core_audio> audio{};

    std::shared_ptr<lite_obs_output> output{};
    std::shared_ptr<lite_obs_encoder> video_encoder{};
    std::shared_ptr<lite_obs_encoder> audio_encoder{};

    lite_obs_private() {
        video = std::make_shared<lite_obs_core_video>();
        audio = std::make_shared<lite_obs_core_audio>();
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
    d_ptr = std::make_unique<lite_obs_private>();
}

lite_obs::~lite_obs()
{
    d_ptr->video->lite_obs_stop_video();
    d_ptr->audio->lite_obs_stop_audio();
}

#define OBS_SIZE_MIN 2
#define OBS_SIZE_MAX (32 * 1024)

static inline bool size_valid(uint32_t width, uint32_t height)
{
    return (width >= OBS_SIZE_MIN && height >= OBS_SIZE_MIN &&
            width <= OBS_SIZE_MAX && height <= OBS_SIZE_MAX);
}

int lite_obs::obs_reset_video(output_video_info *ovi)
{
    if (d_ptr->video->lite_obs_video_active())
        return OBS_VIDEO_CURRENTLY_ACTIVE;

    if (!size_valid(ovi->output_width, ovi->output_height) ||
            !size_valid(ovi->base_width, ovi->base_height))
        return OBS_VIDEO_INVALID_PARAM;

    d_ptr->video->lite_obs_stop_video();

    ovi->output_width &= 0xFFFFFFFC;
    ovi->output_height &= 0xFFFFFFFE;

    return d_ptr->video->lite_obs_start_video(ovi);
}

bool lite_obs::obs_reset_audio(output_audio_info *oai)
{
    return d_ptr->audio->lite_obs_start_audio(oai);
}

bool lite_obs::lite_obs_start_output(std::string output_info, int vb, int ab, std::shared_ptr<lite_obs_output_callbak> callback)
{
    if (!d_ptr->output)
    {
        auto output = std::make_shared<mpeg_ts_output>();
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

std::shared_ptr<lite_source> lite_obs::lite_obs_create_source(source_type type)
{
    auto source = std::make_shared<lite_source>(type, d_ptr->video, d_ptr->audio);
    std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
    lite_source::sources.push_back(std::make_pair(type, source));

    return source;
}

void lite_obs::lite_obs_release_source(std::shared_ptr<lite_source> &source)
{
    {
        std::lock_guard<std::recursive_mutex> lock(lite_source::sources_mutex);
        for (auto iter = lite_source::sources.begin(); iter != lite_source::sources.end(); iter++) {
            auto &pair = *iter;
            if (pair.second == source) {
                lite_source::sources.erase(iter);
                break;
            }
        }
    }

    source.reset();
}
