#pragma once

#include <memory>
#include <string>
#include "lite_encoder_info.h"
#include "lite_obs_callback.h"

class video_output;
class audio_output;
class lite_obs_core_video;
class lite_obs_core_audio;
class lite_obs_encoder;
struct lite_obs_output_private;

#define LITE_OBS_OUTPUT_SUCCESS 0
#define LITE_OBS_OUTPUT_BAD_PATH -1
#define LITE_OBS_OUTPUT_CONNECT_FAILED -2
#define LITE_OBS_OUTPUT_INVALID_STREAM -3
#define LITE_OBS_OUTPUT_ERROR -4
#define LITE_OBS_OUTPUT_DISCONNECTED -5
#define LITE_OBS_OUTPUT_NO_SPACE -7
#define LITE_OBS_OUTPUT_ENCODE_ERROR -8

class lite_obs_output : public std::enable_shared_from_this<lite_obs_output>
{
    friend class lite_obs_encoder;
public:
    lite_obs_output();
    ~lite_obs_output();

    virtual void i_set_output_info(const std::string &info){ }
    virtual bool i_output_valid() = 0;
    virtual bool i_has_video() = 0;
    virtual bool i_has_audio() = 0;
    virtual bool i_encoded() = 0;
    virtual bool i_create() = 0;
    virtual void i_destroy() = 0;
    virtual bool i_start() = 0;
    virtual void i_stop(uint64_t ts) = 0;
    virtual void i_raw_video(struct video_data *frame) = 0;
    virtual void i_raw_audio(struct audio_data *frames) = 0;
    virtual void i_encoded_packet(std::shared_ptr<struct encoder_packet> packet) = 0;
    virtual uint64_t i_get_total_bytes() = 0;
    virtual int i_get_dropped_frames() = 0;
    virtual std::string i_cdn_ip() { return std::string(); }

    void set_output_signal_callback(lite_obs_output_callbak callback);
    const lite_obs_output_callbak &output_signal_callback();

    bool lite_obs_output_create(std::shared_ptr<lite_obs_core_video> c_v, std::shared_ptr<lite_obs_core_audio> c_a);
    void lite_obs_output_destroy();

    bool lite_obs_output_start();
    void lite_obs_output_stop();

    std::shared_ptr<video_output> lite_obs_output_video();
    std::shared_ptr<audio_output> lite_obs_output_audio();

    void lite_obs_output_set_video_encoder(std::shared_ptr<lite_obs_encoder> encoder);
    void lite_obs_output_set_audio_encoder(std::shared_ptr<lite_obs_encoder> encoder, size_t idx);
    std::shared_ptr<lite_obs_encoder> lite_obs_output_get_video_encoder();
    std::shared_ptr<lite_obs_encoder> lite_obs_output_get_audio_encoder(size_t idx);

    uint64_t lite_obs_output_get_total_bytes();
    int lite_obs_output_get_frames_dropped();
    int lite_obs_output_get_total_frames();

    void lite_obs_output_set_preferred_size(uint32_t width, uint32_t height);
    uint32_t lite_obs_output_get_width();
    uint32_t lite_obs_output_get_height();

    void lite_obs_output_set_video_conversion(const struct video_scale_info *conversion);
    void lite_obs_output_set_audio_conversion(const struct audio_convert_info *conversion);

    bool lite_obs_output_can_begin_data_capture();

    bool lite_obs_output_initialize_encoders();

    bool lite_obs_output_begin_data_capture();
    static void end_data_capture_thread(void *data);
    void lite_obs_output_end_data_capture();

    void lite_obs_output_signal_stop(int code);

    void lite_obs_output_set_last_error(const std::string &error);
    const std::string &lite_obs_output_last_error();

private:
    void lite_obs_output_remove_encoder(std::shared_ptr<lite_obs_encoder> encoder);
    void lite_obs_output_flush_packet();

private:
    void interleave_packets_internal(const std::shared_ptr<encoder_packet> &packet);
    static void interleave_packets(void *data, const std::shared_ptr<struct encoder_packet> &packet);

    void default_encoded_callback_internal(const std::shared_ptr<encoder_packet> &packet);
    static void default_encoded_callback(void *param, const std::shared_ptr<encoder_packet> &packet);

    void default_raw_video_callback_internal(struct video_data *frame);
    static void default_raw_video_callback(void *param, struct video_data *frame);

    bool prepare_audio(const struct audio_data *old_data, struct audio_data *new_data);
    void default_raw_audio_callback_internal(size_t mix_idx, struct audio_data *in);
    static void default_raw_audio_callback(void *param, size_t mix_idx, struct audio_data *in);

    void discard_to_idx(size_t idx);
    void discard_unused_audio_packets(int64_t dts_usec);
    void apply_interleaved_packet_offset(std::shared_ptr<struct encoder_packet>out);
    void check_received(std::shared_ptr<encoder_packet> out);
    void insert_interleaved_packet(std::shared_ptr<encoder_packet> out);
    void set_higher_ts(std::shared_ptr<encoder_packet> packet);

    std::shared_ptr<encoder_packet> find_first_packet_type(obs_encoder_type type, size_t audio_idx);
    std::shared_ptr<encoder_packet> find_last_packet_type(obs_encoder_type type, size_t audio_idx);
    auto find_first_packet_type_idx(obs_encoder_type type, size_t audio_idx);
    auto find_last_packet_type_idx(obs_encoder_type type, size_t audio_idx);

    auto get_interleaved_start_idx();
    auto prune_premature_packets();
    bool prune_interleaved_packets();
    bool get_audio_and_video_packets(std::shared_ptr<encoder_packet> &video, std::shared_ptr<encoder_packet> &audio);
    bool initialize_interleaved_packets();
    void resort_interleaved_packets();
    bool has_higher_opposing_ts(std::shared_ptr<encoder_packet> packet);
    void send_interleaved();

    static void reconnect_thread(void *param);
    bool can_reconnect(int code);
    void output_reconnect();

private:
    void free_packets();
    bool has_scaling();
    struct video_scale_info *get_video_conversion();
    struct audio_convert_info *get_audio_conversion();

    bool stopping();

    bool lite_obs_output_actual_start();

    bool can_begin_data_capture(bool encoded, bool has_video, bool has_audio);
    void hook_data_capture(bool encoded, bool has_video, bool has_audio);

    void reset_raw_output();
    void pair_encoders();

    void reset_packet_data();

    void log_frame_info();
    void end_data_capture_thread_internal();
    void obs_output_stop_internal();
    void lite_obs_output_end_data_capture_internal(bool sig);

    void clear_audio_buffers();

private:
    std::unique_ptr<lite_obs_output_private> d_ptr{};
};
