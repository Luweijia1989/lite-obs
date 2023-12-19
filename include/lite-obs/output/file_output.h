#pragma once

#include "lite-obs/lite_obs_output.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

struct lite_ffmpeg_mux_private;
class lite_ffmpeg_mux : public lite_obs_output
{
public:
    lite_ffmpeg_mux();
    virtual  ~lite_ffmpeg_mux();

    virtual void i_set_output_info(void *info) override;
    virtual bool i_output_valid() override;
    virtual bool i_has_video() override;
    virtual bool i_has_audio() override;
    virtual bool i_encoded() override;
    virtual bool i_create() override;
    virtual void i_destroy() override;
    virtual bool i_start() override;
    virtual void i_stop(uint64_t ts) override;
    virtual void i_raw_video(struct video_data *frame) override;
    virtual void i_raw_audio(struct audio_data *frames) override;
    virtual void i_encoded_packet(std::shared_ptr<struct encoder_packet> packet) override;
    virtual uint64_t i_get_total_bytes() override;
    virtual int i_get_dropped_frames() override;

private:
    void init_params();
    bool new_stream(AVStream **stream, const char *name);
    void create_video_stream();
    void create_audio_stream();
    bool init_streams();
    void free_avformat();
    bool open_output_file();
    bool mux_init();
    void deactivate(int code);

private:
    std::unique_ptr<lite_ffmpeg_mux_private> d_ptr{};
};
