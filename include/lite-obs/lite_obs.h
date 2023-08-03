#ifndef LITE_OBS_H
#define LITE_OBS_H

#include <memory>
#include <string>
#include "media-io/audio_output.h"
#include "media-io/video_output.h"
#include "export.hpp"

enum source_type {
    Source_Video = 1 << 0,
    Source_Audio = 1 << 1,
    Source_Async = 1 << 2,
    Source_AsyncVideo = Source_Async | Source_Video,
    Source_AudioVideo = Source_Audio | Source_AsyncVideo,
};

struct lite_obs_data;
struct lite_obs_private;

class lite_source;
class lite_obs_output;
class lite_obs_encoder;
class lite_obs_core_video;
class lite_obs_core_audio;

class lite_obs_output_callbak
{
public:
    virtual void start() = 0;
    virtual void stop(int code, std::string msg) = 0;
    virtual void starting() = 0;
    virtual void stopping() = 0;
    virtual void activate() = 0;
    virtual void deactivate() = 0;
    virtual void connected() = 0;
    virtual void reconnect() = 0;
    virtual void reconnect_success() = 0;
    virtual void first_media_packet() = 0;
};

class lite_obs
{
public:
    struct output_video_info {
        uint32_t fps_num{};
        uint32_t fps_den{};

        uint32_t base_width{};
        uint32_t base_height{};

        uint32_t output_width{};
        uint32_t output_height{};

        video_format output_format{};
    };

    struct output_audio_info {
        uint32_t samples_per_sec{};
        enum speaker_layout speakers{};
    };

    lite_obs();
    ~lite_obs();

    int obs_reset_video(output_video_info *ovi);
    bool obs_reset_audio(output_audio_info *oai);

    bool lite_obs_start_output(std::string output_info, int vb, int ab, std::shared_ptr<lite_obs_output_callbak> callback);
    void lite_obs_stop_output();

    std::shared_ptr<lite_source> lite_obs_create_source(source_type type);
    void lite_obs_release_source(std::shared_ptr<lite_source> &source);

private:
    std::unique_ptr<lite_obs_private> d_ptr{};
};

#endif // LITE_OBS_H
