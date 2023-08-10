#ifndef LITE_OBS_H
#define LITE_OBS_H

#include <memory>
#include <string>
#include "lite_obs_defines.h"
#include "export.hpp"

enum source_type {
    Source_Video = 1 << 0,
    Source_Audio = 1 << 1,
    Source_Async = 1 << 2,
    Source_AsyncVideo = Source_Async | Source_Video,
    Source_AudioVideo = Source_Audio | Source_AsyncVideo,
};

class LITE_OBS_EXPORT lite_obs_output_callbak
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

struct lite_obs_private;
class lite_obs_media_source;
class LITE_OBS_EXPORT lite_obs
{
public:
    lite_obs();
    ~lite_obs();

    static void set_log_callback(void (*log_callback)(int, const char *));

    int obs_reset_video(uint32_t width, uint32_t height, uint32_t fps);
    bool obs_reset_audio(uint32_t sample_rate);

    lite_obs_media_source *lite_obs_create_source(source_type type);
    void lite_obs_destroy_source(lite_obs_media_source *source);

    bool lite_obs_start_output(std::string output_info, int vb, int ab, std::shared_ptr<lite_obs_output_callbak> callback);
    void lite_obs_stop_output();

private:
    lite_obs_private* d_ptr{};
};

struct lite_obs_media_source_private;
class LITE_OBS_EXPORT lite_obs_media_source
{
    friend class lite_obs;
public:
    lite_obs_media_source();
    void output_audio(const uint8_t *audio_data[MAX_AV_PLANES], uint32_t frames, audio_format format, speaker_layout layout, uint32_t sample_rate);
    void output_video(int texId, uint32_t width, uint32_t height);

    void set_pos(float x, float y);
    void set_scale(float s_w, float s_h);

private:
    ~lite_obs_media_source();

private:
    lite_obs_media_source_private *d_ptr{};
};

#endif // LITE_OBS_H
