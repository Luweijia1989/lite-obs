#pragma once

#include "lite-obs/lite_output.h"

struct null_output_private;
class null_output : public lite_obs_output
{
public:
    null_output();
    virtual bool i_output_valid();
    virtual bool i_has_video();
    virtual bool i_has_audio();
    virtual bool i_encoded();
    virtual bool i_create();
    virtual void i_destroy();
    virtual bool i_start();
    virtual void i_stop(uint64_t ts);
    virtual void i_raw_video(struct video_data *frame);
    virtual void i_raw_audio(struct audio_data *frames);
    virtual void i_encoded_packet(std::shared_ptr<struct encoder_packet> packet);
    virtual uint64_t i_get_total_bytes();
    virtual int i_get_dropped_frames();

    static void stop_thread(void *data);

private:
    std::shared_ptr<null_output_private> d_ptr{};
};
