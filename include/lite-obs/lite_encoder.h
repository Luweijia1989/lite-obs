#pragma once

#include <memory>
#include <functional>
#include "lite_encoder_info.h"

struct lite_obs_encoder_private;

typedef void (*new_packet)(void *param, const std::shared_ptr<encoder_packet> &packet);

class lite_obs_core_video;
class lite_obs_core_audio;
class video_output;
class audio_output;
class lite_obs_output;
class lite_obs_encoder;

class lite_obs_encoder_interface
{
public:
    lite_obs_encoder_interface(lite_obs_encoder *ec) : encoder(ec) {}
    virtual const char *i_encoder_codec() = 0;
    virtual obs_encoder_type i_encoder_type() = 0;
    virtual void i_set_gs_render_ctx(void *ctx) { (void)ctx; }
    virtual bool i_create() = 0;
    virtual void i_destroy() = 0;
    virtual bool i_encoder_valid() = 0;
    virtual bool i_encode(encoder_frame *frame, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off) = 0;
    virtual bool i_encode(int tex_id, std::shared_ptr<encoder_packet> packet, std::function<void(std::shared_ptr<encoder_packet>)> send_off) { return true; }
    virtual size_t i_get_frame_size() { return 0; }
    virtual bool i_get_extra_data(uint8_t **extra_data, size_t *size) { return false; }
    virtual bool i_get_sei_data(uint8_t **sei_data, size_t *size) { return false; }
    virtual void i_get_audio_info(struct audio_convert_info *info) { }
    virtual void i_get_video_info(struct video_scale_info *info) { }
    virtual bool i_gpu_encode_available() { return false; }
    virtual void i_update_encode_bitrate(int bitrate) { }

public:
    lite_obs_encoder *encoder = nullptr;
};

class lite_obs_encoder : public std::enable_shared_from_this<lite_obs_encoder>
{
    friend class lite_obs_core_video;
public:
    enum class encoder_id {
        None,
        AAC,
        FFMPEG_H264_HW,
        X264,
        MEDIACODEC,
    };

    lite_obs_encoder(encoder_id id, int bitrate, size_t mixer_idx);
    ~lite_obs_encoder();

    obs_encoder_type lite_obs_encoder_type();
    bool lite_obs_encoder_reset_encoder_impl(encoder_id id);

    void lite_obs_encoder_update_bitrate(int bitrate);
    int lite_obs_encoder_bitrate();

    const char *lite_obs_encoder_codec();

    void lite_obs_encoder_set_scaled_size(uint32_t width, uint32_t height);
    bool lite_obs_encoder_scaling_enabled();

    uint32_t lite_obs_encoder_get_width();
    uint32_t lite_obs_encoder_get_height();
    uint32_t lite_obs_encoder_get_sample_rate();
    size_t lite_obs_encoder_get_frame_size();

    void lite_obs_encoder_set_preferred_video_format(video_format format);
    video_format lite_obs_encoder_get_preferred_video_format();

    bool lite_obs_encoder_get_extra_data(uint8_t **extra_data, size_t *size);

    void lite_obs_encoder_set_core_video(std::shared_ptr<lite_obs_core_video> c_v);
    void lite_obs_encoder_set_core_audio(std::shared_ptr<lite_obs_core_audio> c_a);

    std::shared_ptr<video_output> lite_obs_encoder_video();
    std::shared_ptr<audio_output> lite_obs_encoder_audio();

    bool lite_obs_encoder_active();
    std::shared_ptr<lite_obs_encoder> lite_obs_encoder_paired_encoder();
    void lite_obs_encoder_set_paired_encoder(std::shared_ptr<lite_obs_encoder> encoder);
    void lite_obs_encoder_set_lock(bool lock);
    void lite_obs_encoder_set_wait_for_video(bool wait);

    void lite_obs_encoder_set_sei(char *sei, int len);
    void lite_obs_encoder_clear_sei();
    bool lite_obs_encoder_get_sei(uint8_t *sei, int *sei_len);
    void lite_obs_encoder_set_sei_rate(uint32_t rate);
    uint32_t lite_obs_encoder_get_sei_rate();

    bool obs_encoder_initialize();
    void obs_encoder_shutdown();
    void obs_encoder_start(new_packet cb, void *param);
    void obs_encoder_stop(new_packet cb, void *param);
    void obs_encoder_add_output(std::shared_ptr<lite_obs_output> output);
    void obs_encoder_remove_output(std::shared_ptr<lite_obs_output> output);
    bool start_gpu_encode();
    void stop_gpu_encode();
    void obs_encoder_destroy();

private:
    void clear_audio();
    size_t calc_offset_size(uint64_t v_start_ts, uint64_t a_start_ts);
    void push_back_audio(struct audio_data *data, size_t size, size_t offset_size);
    void start_from_buffer(uint64_t v_start_ts);
    bool buffer_audio(struct audio_data *data);
    bool send_audio_data();
    void receive_audio_internal(size_t mix_idx, struct audio_data *data);
    static void receive_audio(void *param, size_t mix_idx, struct audio_data *data);
    void receive_video_internal(struct video_data *frame);
    static void receive_video(void *param, struct video_data *frame);
    void receive_video_texture(uint64_t timestamp, int tex_id);

    bool encode_send(void *data, bool raw_mem_data, std::shared_ptr<encoder_packet> pkt);
    bool do_encode(encoder_frame *frame);

private:
    std::shared_ptr<lite_obs_encoder_interface> create_encoder(encoder_id id);

    auto get_callback_idx(new_packet cb, void *param);

    void get_audio_info_internal(audio_convert_info *info);
    void reset_audio_buffers();
    void free_audio_buffers();
    void intitialize_audio_encoder();
    bool obs_encoder_initialize_internal();

    void get_video_info_internal(video_scale_info *info);

    void add_connection();
    void remove_connection(bool shutdown);

    void obs_encoder_start_internal(new_packet cb, void *param);
    bool obs_encoder_stop_internal(new_packet cb, void *param);

    void full_stop();
    void obs_encoder_actually_destroy();

    void send_first_video_packet(struct encoder_callback *cb, std::shared_ptr<encoder_packet> packet);
    void send_packet(struct encoder_callback *cb, std::shared_ptr<encoder_packet> packet);

private:
    std::unique_ptr<lite_obs_encoder_private> d_ptr{};
};
