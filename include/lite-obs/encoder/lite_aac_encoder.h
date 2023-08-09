#pragma once

#include "lite-obs/lite_encoder.h"

struct lite_aac_encoder_private;
class lite_aac_encoder : public lite_obs_encoder
{
public:
    lite_aac_encoder(int bitrate, size_t mixer_idx);
    virtual ~lite_aac_encoder();

    virtual const char *i_encoder_codec();
    virtual obs_encoder_type i_encoder_type();
    virtual bool i_create();
    virtual void i_destroy();
    virtual bool i_encoder_valid();
    virtual bool i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off);
    virtual size_t i_get_frame_size();
    virtual bool i_get_extra_data(uint8_t **extra_data, size_t *size);
    virtual void i_get_audio_info(struct audio_convert_info *info);

private:
    bool i_encode_internal(std::shared_ptr<encoder_packet> packet, bool *received_packet);

private:
    void init_sizes(std::shared_ptr<audio_output> audio);
    bool initialize_codec();

private:
    std::unique_ptr<lite_aac_encoder_private> d_ptr{};
};
